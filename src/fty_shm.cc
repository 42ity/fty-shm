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
#include <unistd.h>
#include <unordered_set>
#include <regex>
#include <iostream>
#include <map>

#include "fty_shm.h"
#include "internal.h"

#define DEFAULT_SHM_DIR "/var/run/fty-shm-1"

#define SEPARATOR '@'
#define SEPARATOR_LEN 1

// The first 11 bytes of each file are the ttl in 10 decimal digits, followed
// by \n.  This is a compromise between machine and human readability
#define TTL_FMT "%010d\n"
#define TTL_LEN 11

// The next 11 bytes specify the unit of the metric, right-padded with spaces
// and followed by \n
#define UNIT_FMT "%-10.10s\n"
#define UNIT_LEN 11

#define HEADER_LEN (TTL_LEN + UNIT_LEN)
#define PAYLOAD_LEN (128 - HEADER_LEN)

// Convenience macros
#define FREE(x) (free(x), (x) = NULL)

// This is only changed by the selftest code
static const char* shm_dir = DEFAULT_SHM_DIR;
static size_t shm_dir_len = strlen(DEFAULT_SHM_DIR);

static int prepare_filename(char* buf, const char* asset, size_t a_len, const char* metric, size_t m_len, const char* type)
{
    if (m_len + SEPARATOR_LEN + a_len  > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (memchr(asset, '/', a_len) || memchr(asset, SEPARATOR, a_len) ||
            memchr(metric, '/', m_len) || memchr(metric, SEPARATOR, m_len)) {
        errno = EINVAL;
        return -1;
    }
    char* p = buf;
    memcpy(p, shm_dir, shm_dir_len);
    p += shm_dir_len;

    *p++ = '/';
    memcpy(p, type, strlen(type));
    p += strlen(type);

    *p++ = '/';
    memcpy(p, metric, m_len);
    p += m_len;
    *p++ = SEPARATOR;
    memcpy(p, asset, a_len);
    p += a_len;
    *p++ = '\0';
    return 0;
}

static int prepare_filename(char* buf, const char* asset, size_t a_len, const char* metric, size_t m_len)
{
  return prepare_filename(buf, asset, a_len, metric, m_len, "metric");
}

// Assumes len is small enough for the read to be atomic (i.e. <= 4k)
static ssize_t read_buf(int fd, char* buf, size_t len)
{
    ssize_t ret = read(fd, buf, len);
    if (ret >= 0 && static_cast<size_t>(ret) != len) {
        errno = EIO;
        return -1;
    }
    return ret;
}

int fty_write_nut_metric(std::string asset, std::string metric, std::string value, int ttl) {
  return fty::shm::write_nut_metric(asset, metric, value, ttl);
}

int fty::shm::write_nut_metric(std::string asset, std::string metric, std::string value, int ttl) {
  //TODO : convert nut metric name to fty metric name and select the right unit.
  return write_metric(asset, metric, value, "NULL", ttl);
}

// Write ttl and value to filename
static int write_value(const char* filename, const char* value, const char* unit, int ttl)
{
    int fd;
    char buf[HEADER_LEN + PAYLOAD_LEN];
    size_t value_len;
    int err = 0;

    value_len = strlen(value);
    if (value_len > PAYLOAD_LEN) {
        errno = EINVAL;
        return -1;
    }
    if ((fd = open(filename, O_CREAT | O_RDWR | O_CLOEXEC, 0666)) < 0)
        return -1;
    if (ttl < 0)
        ttl = 0;
    sprintf(buf, TTL_FMT, ttl);
    sprintf(buf + TTL_LEN, UNIT_FMT, unit);
    memcpy(buf + HEADER_LEN, value, value_len);
    memset(buf + HEADER_LEN + value_len, 0, sizeof(buf) - HEADER_LEN - value_len);
    if (pwrite(fd, buf, sizeof(buf), 0) < 0)
        err = -1;
    if (close(fd) < 0)
        err = -1;
    return err;
}

static char* dup_str(char *str, char*)
{
    return strdup(str);
}

// When working with std::string, we do not want to call strdup
static char* dup_str(char *str, std::string)
{
    return str;
}

static int parse_ttl(char* ttl_str, time_t& ttl)
{
    char *err;
    int res;

    // Delete the '\n'
    int len = strlen(ttl_str) -1;
    if(ttl_str[len] == '\n')
      ttl_str[len] = '\0';
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
    char buf[HEADER_LEN + PAYLOAD_LEN];
    time_t now, ttl;
    int ret = -1;

    if ((fd = open(filename, O_RDONLY | O_CLOEXEC)) < 0)
        return ret;
    if (fstat(fd, &st) < 0)
        goto out_fd;
    if (read_buf(fd, buf, sizeof(buf)) < 0)
        goto out_fd;

    buf[TTL_LEN - 1] = '\0';
    if (parse_ttl(buf, ttl) < 0)
        goto out_fd;
    if (ttl) {
        now = time(NULL);
        if (now - st.st_mtime > ttl) {
            errno = ESTALE;
            goto out_fd;
        }
    }
    if (need_unit) {
        char *unit_buf = buf + TTL_LEN;
        // Trim the padding spaces
        int i = UNIT_LEN - 1;
        while (unit_buf[i] == ' ' || unit_buf[i] == '\n')
            --i;
        unit_buf[i + 1] = '\0';
        unit = dup_str(unit_buf, T());
    }
    value = dup_str(buf + HEADER_LEN, T());
    ret = 0;

out_fd:
    close(fd);
    return ret;
}

int read_data_metric(const char* filename, fty_proto_t *proto_metric) {
  int ret = -1;
  struct stat st;
  FILE* file = NULL;
  char buf[128];
  char bufVal[128];
  time_t now, ttl;
  int len;

  file = fopen(filename, "r");
  if(file == NULL)
    goto shm_out_fd;
  if(fstat(fileno(file), &st) < 0)
    goto shm_out_fd;

  //get ttl
  fgets(buf, sizeof(buf), file);
  
  if (parse_ttl(buf, ttl) < 0)
    goto shm_out_fd;

  //data still valid ?
  if (ttl) {
        now = time(NULL);
        if (now - st.st_mtime > ttl) {
            errno = ESTALE;
            goto shm_out_fd;
        }
    }

  //set ttl
  fty_proto_set_ttl(proto_metric,ttl);
  //set timestamp
  fty_proto_set_time(proto_metric, st.st_mtim.tv_sec);

  //get unit
  fgets(buf, sizeof(buf), file);
  // Delete the '\n'
  len = strlen(buf) -1;
  if(buf[len] == '\n')
    buf[len] = '\0';
  fty_proto_set_unit(proto_metric, buf);

  //get value
  fgets(buf, sizeof(buf), file);
  // Delete the '\n'
  len = strlen(buf) -1;
  if(buf[len] == '\n')
    buf[len] = '\0';

  fty_proto_set_value(proto_metric, buf);

  while(fgets(buf, sizeof(buf), file) != NULL) {
    if(fgets(bufVal, sizeof(bufVal), file) == NULL) {
      break;
    }

    // Delete the '\n'
    len = strlen(buf) -1;
    if(buf[len] == '\n')
      buf[len] = '\0';

    len = strlen(bufVal) -1;
    if(bufVal[len] == '\n')
      bufVal[len] = '\0';

    fty_proto_aux_insert(proto_metric, buf, "%s", bufVal);
  }
  fclose(file);
  return 0;

shm_out_fd:
    fclose(file);
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


int fty_shm_read_family(const char* family, std::string asset, std::string type, fty::shm::shmMetrics& result)
{
  char* working_dir = (char*) malloc(250);
  getcwd(working_dir, 250);
  std::string family_dir = shm_dir;
  family_dir.append("/");
  family_dir.append(family);
  DIR* dir;
  if(!(dir = opendir(family_dir.c_str())))
    return -1;
  chdir(family_dir.c_str());
  //fchdir(dirfd(dir));
  struct dirent* de;

  try {

    std::regex regType(type);
    std::regex regAsset(asset);
    std::cout << std::endl;
    while ((de = readdir(dir))) {
      const char* delim = strchr(de->d_name, SEPARATOR);
      //If not a valid metric
      if(!delim)
        continue;
      size_t type_name = delim - de->d_name;
      if(std::regex_match(std::string(delim+1), regAsset) && std::regex_match(std::string(de->d_name, type_name), regType)) {
        fty_proto_t *proto_metric = fty_proto_new(FTY_PROTO_METRIC);
        if(read_data_metric(de->d_name, proto_metric) == 0) {
          fty_proto_set_name(proto_metric, "%s", std::string(delim+1).c_str());
          fty_proto_set_type(proto_metric, "%s", std::string(de->d_name, type_name).c_str());
          result.add(proto_metric);
        } else {
          fty_proto_destroy(&proto_metric);
        }
      }
    }
  } catch(const std::regex_error& e) {
    chdir(working_dir);
    return -1;
  }
  chdir(working_dir);
  return 0;
}

int fty::shm::read_metrics(const std::string& family, const std::string& asset, const std::string& type, shmMetrics& result)
{
  DIR* dir;
  if(family == "*") {
    struct dirent *de_root;
    if (!(dir = opendir(shm_dir)))
        return -1;
    dirfd(dir);
    while ((de_root = readdir(dir))) {
      fty_shm_read_family(de_root->d_name, asset, type, result);
    }
  }
  else {
    fty_shm_read_family(family.c_str(), asset, type, result);
  }
  return 0;
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
    DIR* dir_child;
    int  dfd;
    struct dirent *de, *de_root;
    int err = 0;
    std::string shm_subdir;

    if (!(dir = opendir(shm_dir)))
        return -1;
    dirfd(dir);

    while ((de_root = readdir(dir))) {
      shm_subdir = shm_dir;
      shm_subdir.append("/");
      shm_subdir.append(de_root->d_name);
      if(!(dir_child = opendir(shm_subdir.c_str())))
        continue;
      dfd = dirfd(dir_child);
      while ((de = readdir(dir_child))) {
          int fd;
          time_t now, ttl;
          struct stat st1, st2;
          char ttl_str[TTL_LEN];

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
      closedir(dir_child);
    }
    closedir(dir);
    return err;
}

// Write ttl and value to filename
static int write_metric_data(const char* filename, fty_proto_t* metric)
{
    FILE* file = fopen(filename, "w");
    if(file == NULL)
      return -1;
    int ttl = fty_proto_ttl(metric);
    if (ttl < 0)
        ttl = 0;
    fprintf(file, "%d\n%s\n%s", ttl, fty_proto_unit(metric), fty_proto_value(metric));
    zhash_t *aux = fty_proto_aux(metric);

    if (aux) {
      char *item = (char *) zhash_first (aux);
      while (item) {
          fprintf (file, "\n%s\n%s", zhash_cursor (aux), item);
          item = (char *) zhash_next (aux);
      }
    }
    if(fclose(file) < 0)
      return -1;

    return 0;
}


int fty::shm::write_metric(fty_proto_t* metric)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, fty_proto_name(metric), strlen(fty_proto_name(metric)), fty_proto_type(metric), strlen(fty_proto_type(metric))) < 0)
        return -1;
    return write_metric_data(filename, metric);
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

/*int fty::shm::find_assets(Assets& assets)
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
}*/

int fty::shm::read_asset_metrics(const std::string& asset, Metrics& metrics)
{
    DIR* dir;
    struct dirent* de;
    int err = -1;

    std::string shm_dirmetrics = shm_dir;
    shm_dirmetrics.append("/");
    shm_dirmetrics.append("metric");
    if (!(dir = opendir(shm_dirmetrics.c_str())))
        return -1;

    metrics.clear();
    while ((de = readdir(dir))) {
        const char* delim = strchr(de->d_name, SEPARATOR);
        size_t metric_len = delim - de->d_name;
        if (std::string(delim+1) != asset)
            continue;
        Metric metric;
        char filename[PATH_MAX];
        sprintf(filename, "%s/%s", shm_dirmetrics.c_str(), de->d_name);
        if (read_value(filename, metric.value, metric.unit) < 0)
            continue;
        err = 0;
        metrics.emplace(std::string(delim + 1, metric_len), metric);
    }
    closedir(dir);
    return err;
}

fty::shm::shmMetrics::~shmMetrics() {
  for (std::vector<fty_proto_t *>::iterator i = m_metricsVector.begin(); i != m_metricsVector.end(); ++i) {
    fty_proto_destroy(&(*i));
  }
  m_metricsVector.clear();
}

fty_proto_t* fty::shm::shmMetrics::get(int i) {
  return m_metricsVector.at(i);
}

fty_proto_t* fty::shm::shmMetrics::getDup(int i) {
  return fty_proto_dup(m_metricsVector.at(i));
}

long unsigned int fty::shm::shmMetrics::size() {
  return m_metricsVector.size();
}

void fty::shm::shmMetrics::add(fty_proto_t* metric) {
  m_metricsVector.push_back(metric);
}

void init_default_dir() {
  chdir(DEFAULT_SHM_DIR);
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
    //TODO
    printf("OK\n");
}
