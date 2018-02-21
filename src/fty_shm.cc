/*  =========================================================================
    fty_shm - FTY metric sharing functions

    Copyright (C) 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    fty_shm - FTY metric sharing functions
@discuss
@end
*/

#include <algorithm>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <random>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <unordered_set>

#include "fty_shm.h"
#include "internal.h"

#define DEFAULT_SHM_DIR "/var/run/fty-shm-1"
#define METRIC_SUFFIX ".metric"
#define SUFFIX_LEN (sizeof(METRIC_SUFFIX) - 1)

// The first 11 bytes of each file are the ttl in 10 decimal digits, followed
// by \n.  This is a compromise between machine and human readability
#define TTL_FMT "%010d\n"
#define TTL_LEN 11

// The next 11 bytes specify the unit of the metric, right-padded with spaces
// and followed by \n
#define UNIT_FMT "%-10.10s\n"
#define UNIT_LEN 11

#define HEADER_LEN (TTL_LEN + UNIT_LEN)

// Convenience macros
#define streq(s1, s2) (strcmp((s1), (s2)) == 0)
#define FREE(x) (free(x), (x) = NULL)

// This is only changed by the selftest code
static const char* shm_dir = DEFAULT_SHM_DIR;
static size_t shm_dir_len = strlen(DEFAULT_SHM_DIR);

static int prepare_filename(char* buf, const char* asset, size_t a_len, const char* metric, size_t m_len)
{
    if (a_len + strlen(":") + m_len + SUFFIX_LEN > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (memchr(asset, '/', a_len) || memchr(asset, ':', a_len) ||
            memchr(metric, '/', m_len) || memchr(metric, ':', m_len)) {
        errno = EINVAL;
        return -1;
    }
    char* p = buf;
    memcpy(p, shm_dir, shm_dir_len);
    p += shm_dir_len;
    *p++ = '/';
    memcpy(p, asset, a_len);
    p += a_len;
    *p++ = ':';
    memcpy(p, metric, m_len);
    p += m_len;
    memcpy(p, METRIC_SUFFIX, sizeof(METRIC_SUFFIX));
    return 0;
}

// XXX: May modify iov
static ssize_t write_vec(int fd, struct iovec* iov, int iovcnt)
{
    size_t written = 0, step;
    ssize_t ret;

    while (iovcnt > 0) {
        if ((ret = writev(fd, iov, iovcnt)) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        step = ret;
        written += step;
        while (step && step >= iov[0].iov_len) {
            step -= iov[0].iov_len;
            iov++;
            iovcnt--;
        }
        if (step > 0) {
            iov[0].iov_len -= step;
            iov[0].iov_base = static_cast<char *>(iov[0].iov_base) + step;
        }
    }
    return written;
}

// XXX: May modify iov
static ssize_t read_vec(int fd, struct iovec* iov, int iovcnt)
{
    size_t written = 0, step;
    ssize_t ret;

    while (iovcnt > 0) {
        if ((ret = readv(fd, iov, iovcnt)) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        step = ret;
        written += step;
        while (step && step >= iov[0].iov_len) {
            step -= iov[0].iov_len;
            iov++;
            iovcnt--;
        }
        if (step > 0) {
            iov[0].iov_len -= step;
            iov[0].iov_base = static_cast<char *>(iov[0].iov_base) + step;
        }
    }
    return written;
}

static ssize_t read_buf(int fd, char* buf, size_t len)
{
    struct iovec iov;

    iov.iov_base = buf;
    iov.iov_len = len;
    return read_vec(fd, &iov, 1);
}

// We need to create files according to the umask
static int create_temp(char *buf)
{
    // 2**6 == 64 letters
    static const char letters[] = "0123456789_-"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    // The seed is not of exceptional quality (ASLR-ed stack address XOR time),
    // but since the storage directory is protected by filesystem permissions,
    // we do not need particularly strong randomness.
    int num, fd;
    thread_local static std::minstd_rand rnd(reinterpret_cast<long>(&num) >> 4 ^ time(NULL));
    char *p;

    strcpy(buf, shm_dir);
    p = buf + strlen(shm_dir);
    *p++ = '/';
    for (int attempt = 0; attempt < 100000; ++attempt) {
        int i;
        num = rnd();
        for (i = 0; i < 4; ++i) {
            p[i] = letters[num & 63];
            num >>= 6;
        }
        p[i] = '\0';
        fd = open(buf, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0666);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    return fd;
}

// Write ttl and value to filename.tmp and atomically replace filename
static int write_value(const char* filename, const char* value, const char* unit, int ttl)
{
    int fd;
    char header[HEADER_LEN + 1], tmp[PATH_MAX];
    struct iovec iov[2];

    if ((fd = create_temp(tmp)) < 0)
        return -1;
    if (ttl < 0)
        ttl = 0;
    snprintf(header, TTL_LEN + 1, TTL_FMT, ttl);
    snprintf(header + TTL_LEN, UNIT_LEN + 1, UNIT_FMT, unit);
    iov[0].iov_base = header;
    iov[0].iov_len = HEADER_LEN;
    iov[1].iov_base = const_cast<char*>(value);
    iov[1].iov_len = strlen(value);
    if (write_vec(fd, iov, 2) < 0) {
        close(fd);
        goto out_unlink;
    }
    if (close(fd) < 0)
        goto out_unlink;
    if (rename(tmp, filename) < 0)
        goto out_unlink;
    return 0;

out_unlink:
    unlink(tmp);
    return -1;
}

static bool alloc_str(char*& str, size_t len)
{
    str = (char*)malloc(len + 1);
    if (!str)
        return false;
    str[len] = '\0';
    return true;
}

static bool alloc_str(std::string& str, size_t len)
{
    str.resize(len);
    return true;
}

static void free_str(char*& str)
{
    free(str);
    str = NULL;
}

static void free_str(std::string& str)
{
    // NO-OP
}

static char* str_buf(char* str)
{
    return str;
}

static char* str_buf(std::string& str)
{
    return &str.front();
}


static void trim_str(char*& str, size_t len)
{
    str[len] = '\0';
}

static void trim_str(std::string& str, size_t len)
{
    str.resize(len);
}

static int parse_ttl(char* ttl_str, time_t& ttl)
{
    char *err;
    int res;

    // Delete the '\n'
    ttl_str[TTL_LEN - 1] = '\0';
    res = strtol(ttl_str, &err, 10);
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }
    ttl = res;
    return 0;
}

// XXX: The error codes are somewhat arbitrary
template <typename T>
static int read_value(const char* filename, T& value, T& unit, bool need_unit = true)
{
    int fd;
    struct stat st;
    size_t size;
    T value_buf = T(), unit_buf = T();
    char unit_dummy[UNIT_LEN];
    char ttl_str[TTL_LEN];
    struct iovec iov[3];
    time_t now, ttl;
    int ret = -1;

    if ((fd = open(filename, O_RDONLY | O_CLOEXEC)) < 0)
        return ret;
    if (fstat(fd, &st) < 0)
        goto out_fd;
    if (st.st_size < HEADER_LEN) {
        errno = EIO;
        return -1;
    }
    iov[0].iov_base = ttl_str;
    iov[0].iov_len = TTL_LEN;

    if (need_unit) {
        if (!alloc_str(unit_buf, UNIT_LEN))
            goto out_fd;
        iov[1].iov_base = str_buf(unit_buf);
    } else {
        iov[1].iov_base = unit_dummy;
    }
    iov[1].iov_len = TTL_LEN;

    size = st.st_size - HEADER_LEN;
    if (!alloc_str(value_buf, size))
        goto out_unit;
    iov[2].iov_base = str_buf(value_buf);
    iov[2].iov_len = size;
    if (read_vec(fd, iov, 3) < 0)
        goto out_value;

    if (parse_ttl(ttl_str, ttl) < 0)
        goto out_value;
    if (ttl) {
        now = time(NULL);
        if (now - st.st_mtime > ttl) {
            errno = ESTALE;
            goto out_value;
        }
    }
    if (need_unit) {
        // Trim the padding spaces
        int i = UNIT_LEN - 1;
        while (unit_buf[i] == ' ' || unit_buf[i] == '\n')
            --i;
        trim_str(unit_buf, i + 1);
    }
    value = value_buf;
    if (need_unit)
        unit = unit_buf;
    close(fd);
    return 0;

out_value:
    free_str(value_buf);
out_unit:
    if (need_unit)
        free_str(unit_buf);
out_fd:
    close(fd);
    return ret;
}

int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, asset, strlen(asset), metric, strlen(metric)) < 0)
        return -1;
    return write_value(filename, value, unit, ttl);
}

int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, asset, strlen(asset), metric, strlen(metric)) < 0)
        return -1;
    if (!unit) {
        char* dummy;
        return read_value(filename, *value, dummy, false);
    }
    return read_value(filename, *value, *unit);
}

int fty_shm_delete_asset(const char* asset)
{
    DIR* dir;
    struct dirent* de;
    int err = 0;

    if (!(dir = opendir(shm_dir)))
        return -1;

    while ((de = readdir(dir))) {
        const char* delim = strchr(de->d_name, ':');
        if (!delim)
            // Malformed filename
            continue;
        size_t asset_len = delim - de->d_name;
        if (std::string(de->d_name, asset_len) != asset)
            continue;
        char filename[PATH_MAX];
        sprintf(filename, "%s/%s", shm_dir, de->d_name);
        if (unlink(filename) < 0)
            err = -1;
    }
    closedir(dir);
    return err;
}

int fty_shm_set_test_dir(const char* dir)
{
    if (strlen(dir) > PATH_MAX - strlen("/") - NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    shm_dir = dir;
    shm_dir_len = strlen(dir);
    return 0;
}

// renameat2() is unfortunately Linux-specific and glibc does not even
// provide a wrapper
static int rename_noreplace(int dfd, const char* src, const char* dst)
{
    return syscall(SYS_renameat2, dfd, src, dfd, dst, RENAME_NOREPLACE);
}

int fty_shm_cleanup(bool verbose)
{
    DIR* dir;
    int dfd;
    struct dirent* de;
    int err = 0;

    if (!(dir = opendir(shm_dir)))
        return -1;
    dfd = dirfd(dir);

    while ((de = readdir(dir))) {
        int fd;
        time_t now, ttl;
        struct stat st1, st2;
        size_t len = strlen(de->d_name);
        char ttl_str[TTL_LEN];

        if (len < SUFFIX_LEN)
            // Malformed filename
            continue;
        if (strncmp(de->d_name + len - SUFFIX_LEN, METRIC_SUFFIX, SUFFIX_LEN) != 0)
            // Not a metric
            continue;
        if ((fd = openat(dfd, de->d_name, O_RDONLY | O_CLOEXEC)) < 0) {
            err = -1;
            continue;
        }
        if (fstat(fd, &st1) < 0) {
            err = -1;
            close(fd);
            continue;
        }
        if (st1.st_size < HEADER_LEN) {
            // Malformed file
            close(fd);
            continue;
        }
        if (read_buf(fd, ttl_str, TTL_LEN) < 0) {
            err = -1;
            close(fd);
            continue;
        }
        close(fd);
        if (parse_ttl(ttl_str, ttl) < 0) {
            err = -1;
            continue;
        }
        if (!ttl)
            continue;
        now = time(NULL);
        // We wait for two times the ttl value before deleting the entry
        if ((now - st1.st_mtime) / 2 <= ttl)
            continue;
        // We can race here, but that is not considered a problem. A
        // metric not updated for twice the ttl time is already a bug
        // and the effect of the race is following:
        // 1. Metric expires
        // 2. We check that another ttl seconds have passed
        // 3. Metric gets updated
        // 4. We erroneously delete the updated metric
        // 5. We restore the updated metric
        // i.e. the updated metric disappears briefly between 4. and 5.,
        // while it had been gone for ttl seconds between 1. and 3.
        if (renameat(dfd, de->d_name, dfd, ".delete") < 0) {
            err = -1;
            continue;
        }
        if (fstatat(dfd, ".delete", &st2, 0) < 0) {
            // This should not happen
            err = -1;
            continue;
        }
        if (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino) {
            if (unlinkat(dfd, ".delete", 0) < 0)
                err = -1;
            continue;
        }
        // We lost the race. Restore the metric, but only if it has not
        // been updated for the second time.
        if (rename_noreplace(dfd, ".delete", de->d_name) < 0) {
            unlinkat(dfd, ".delete", 0);
            err = -1;
        }
    }
    closedir(dir);
    return err;
}

int fty::shm::write_metric(const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl)
{
    char filename[PATH_MAX];
    std::string dummy;

    if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length()) < 0)
        return -1;
    return write_value(filename, value.c_str(), unit.c_str(), ttl);
}

int fty::shm::read_metric(const std::string& asset, const std::string& metric, std::string& value)
{
    char filename[PATH_MAX];
    std::string dummy;

    if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length()) < 0)
        return -1;
    return read_value(filename, value, dummy, false);
}

int fty::shm::read_metric(const std::string& asset, const std::string& metric, std::string& value, std::string& unit)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length()) < 0)
        return -1;
    return read_value(filename, value, unit);
}

int fty::shm::find_assets(Assets& assets)
{
    DIR* dir;
    struct dirent* de;
    std::unordered_set<std::string> seen;

    if (!(dir = opendir(shm_dir)))
        return -1;

    // TODO: Remember the number of items from last time and reserve it
    assets.clear();
    while ((de = readdir(dir))) {
        char* delim = strchr(de->d_name, ':');
        if (!delim)
            // Malformed filename
            continue;
        *delim = '\0';
        if (!seen.insert(de->d_name).second)
            continue;
        assets.push_back(de->d_name);
    }
    closedir(dir);
    return 0;
}

int fty::shm::read_asset_metrics(const std::string& asset, Metrics& metrics)
{
    DIR* dir;
    struct dirent* de;
    int err = -1;

    if (!(dir = opendir(shm_dir)))
        return -1;

    metrics.clear();
    while ((de = readdir(dir))) {
        const char* delim = strchr(de->d_name, ':');
        size_t len = strlen(de->d_name);
        if (!delim || len < SUFFIX_LEN)
            // Malformed filename
            continue;
        size_t asset_len = delim - de->d_name;
        size_t metric_len = len - asset_len - strlen(":") - SUFFIX_LEN;
        if (strncmp(de->d_name + len - SUFFIX_LEN, METRIC_SUFFIX, SUFFIX_LEN) != 0)
            // Not a metric
            continue;
        if (std::string(de->d_name, asset_len) != asset)
            continue;
        Metric metric;
        char filename[PATH_MAX];
        sprintf(filename, "%s/%s", shm_dir, de->d_name);
        if (read_value(filename, metric.value, metric.unit) < 0)
            continue;
        err = 0;
        metrics.emplace(std::string(delim + 1, metric_len), metric);
    }
    closedir(dir);
    return err;
}

//  --------------------------------------------------------------------------
//  Self test of this class

// Version of assert() that prints the errno value for easier debugging
#define check_err(expr)                                                   \
    do {                                                                  \
        if ((expr) < 0) {                                                 \
            fprintf(stderr, __FILE__ ":%d: Assertion `%s' failed (%s)\n", \
                __LINE__, #expr, strerror(errno));                        \
            abort();                                                      \
        }                                                                 \
    } while (0)

void fty_shm_test(bool verbose)
{
    char* value = NULL;
    char* unit = NULL;
    std::string cpp_value;
    std::string cpp_unit;
    fty::shm::Metric cpp_result;
    const char *asset1 = "test_asset_1", *asset2 = "test_asset_2";
    const char *metric1 = "test_metric_1", *metric2 = "test_metric_2";
    const char *value1 = "hello world", *value2 = "This is\na metric";
    const char *unit1 = "unit1", *unit2 = "unit2";

    printf(" * fty_shm: ");

    check_err(fty_shm_set_test_dir("src/selftest-rw"));
    check_err(access("src/selftest-rw", X_OK | W_OK));
    // The buildsystem does not delete this for some reason
    assert(system("rm -f src/selftest-rw/*") == 0);

    // Check for invalid characters
    assert(fty_shm_write_metric("invalid/asset", metric1, value1, unit1, 0) < 0);
    assert(fty_shm_read_metric("invalid/asset", metric1, &value, NULL) < 0);
    assert(!value);
    assert(fty_shm_write_metric(asset1, "invalid:metric", value1, unit1, 0) < 0);
    assert(fty_shm_read_metric(asset1, "invalid:metric", &value, NULL) < 0);
    assert(!value);

    // Check for too long asset or metric name
    char* name2long = (char*)malloc(NAME_MAX + 10);
    assert(name2long);
    memset(name2long, 'A', NAME_MAX + 9);
    name2long[NAME_MAX + 9] = '\0';
    assert(fty_shm_write_metric(name2long, metric1, value1, unit1, 300) < 0);
    assert(fty_shm_read_metric(name2long, metric1, &value, NULL) < 0);
    assert(!value);
    assert(fty_shm_write_metric(asset1, name2long, value1, unit1, 300) < 0);
    assert(fty_shm_read_metric(asset1, name2long, &value, NULL) < 0);
    assert(!value);
    free(name2long);

    // Check that the storage is empty
    fty::shm::Assets assets;
    fty::shm::find_assets(assets);
    assert(assets.size() == 0);

    // Write and read back a metric
    check_err(fty_shm_write_metric(asset1, metric1, value1, unit1, 0));
    usleep(1100000);
    check_err(fty_shm_read_metric(asset1, metric1, &value, &unit));
    assert(value);
    assert(streq(value, value1));
    FREE(value);
    assert(unit);
    assert(streq(unit, unit1));
    FREE(unit);
    check_err(fty_shm_read_metric(asset1, metric1, &value, NULL));
    assert(value);
    assert(streq(value, value1));
    FREE(value);

    // Update a metric (C++)
    check_err(fty::shm::write_metric(asset1, metric1, value2, unit2, 0));
    check_err(fty::shm::read_metric(asset1, metric1, cpp_value, cpp_unit));
    assert(cpp_value == value2);
    assert(cpp_unit == unit2);
    cpp_value = "";
    check_err(fty::shm::read_metric(asset1, metric1, cpp_value));
    assert(cpp_value == value2);
    check_err(fty::shm::read_metric(asset1, metric1, cpp_result));
    assert(cpp_result.value == value2);
    assert(cpp_result.unit == unit2);

    // Write a metric as double
    check_err(fty::shm::write_metric(asset1, metric2, 42.0, "%", 0));
    check_err(fty::shm::read_metric(asset1, metric2, cpp_value));
    assert(cpp_value.compare(0, 4, "42.0") == 0 || cpp_value.compare(0, 4, "41.9") == 0);

    // List assets
    check_err(fty_shm_write_metric(asset1, metric2, value1, unit1, 0));
    check_err(fty_shm_write_metric(asset2, metric1, value1, unit1, 0));
    fty::shm::find_assets(assets);
    assert(assets.size() == 2);
    assert(std::find(assets.begin(), assets.end(), asset1) != assets.end());
    assert(std::find(assets.begin(), assets.end(), asset2) != assets.end());

    // Load all metrics for an asset
    fty::shm::Metrics metrics;
    check_err(fty::shm::read_asset_metrics(asset1, metrics));
    assert(metrics.size() == 2);
    assert(metrics[metric1].value == value2);
    assert(metrics[metric1].unit == unit2);
    assert(metrics[metric2].value == value1);
    assert(metrics[metric2].unit == unit1);

    // Delete asset1 and check that asset2 remains
    check_err(fty::shm::delete_asset(asset1));
    assert(fty::shm::read_asset_metrics(asset1, metrics) < 0);
    check_err(fty::shm::read_asset_metrics(asset2, metrics));
    assert(metrics.size() == 1);

    // TTL OK
    check_err(fty_shm_write_metric(asset2, metric1, value2, unit2, INT_MAX));
    check_err(fty_shm_read_metric(asset2, metric1, &value, &unit));
    assert(value);
    assert(streq(value, value2));
    FREE(value);
    assert(unit);
    assert(streq(unit, unit2));
    FREE(unit);

    // TTL expired
    check_err(fty_shm_write_metric(asset1, metric1, value2, unit2, 1));
    sleep(2);
    assert(fty_shm_read_metric(asset1, metric1, &value, NULL) < 0);
    assert(!value);

    // Garbage collector: asset1 expired and must be deleted, asset2 must stay
    sleep(2);
    check_err(fty_shm_cleanup(verbose));
    check_err(access("src/selftest-rw/test_asset_2:test_metric_1.metric", F_OK));
    assert(access("src/selftest-rw/test_asset_1:test_metric_1.metric", F_OK) < 0);

    // Check that we are not leaking file descriptors
    DIR* dir;
    struct dirent* de;
    assert((dir = opendir("/proc/self/fd")));
    bool ok = true;
    // Only stdin, stdout, stderr and the directory fd should be open
    std::unordered_set<std::string> allowed = { ".", "..", "0", "1", "2", std::to_string(dirfd(dir)) };
    while ((de = readdir(dir))) {
        if (atoi(de->d_name) >= 1024)
            // Assume that this is a valgrind internal file descriptor
            continue;
        if (allowed.find(de->d_name) == allowed.end()) {
            printf("File descriptor %s leaked\n", de->d_name);
            ok = false;
        }
    }
    closedir(dir);
    if (!ok)
        abort();
    printf("OK\n");
}
