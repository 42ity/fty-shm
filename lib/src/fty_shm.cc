/*  =========================================================================
    Copyright (C) 2018 - 2020 Eaton

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

/// fty_shm - FTY metric sharing functions

#include "fty_shm.h"
#include "publisher.h"
#include <regex>

#define DEFAULT_SHM_DIR "/run/42shm"

#define SEPARATOR     '@'   //metric @ asset
#define SEPARATOR_LEN 1

// The first 11 bytes of each file are the ttl in 10 decimal digits, followed
// by \n.  This is a compromise between machine and human readability
#define TTL_FMT "%010d\n"
#define TTL_LEN 11

#define UNIT_FMT "%s\n"

#define POLL_ENV        "FTY_SHM_TEST_POLLING_INTERVAL"
#define AUTOCLEAN_ENV   "FTY_SHM_AUTOCLEAN"

#define ZCONFIG_PATH "/etc/fty-nut/fty-nut.cfg"

void fty_shm_set_default_polling_interval(int val)
{
    if (val >= 0) {
        std::string s = std::to_string(val); // sec.
        setenv(POLL_ENV, s.c_str(), 1);
    }
}

int fty_get_polling_interval()
{
    int val = 30; // default (sec.)

    const char* env = getenv(POLL_ENV);
    if (env && (strtol(env, nullptr, 10) > 0)) {
        val = int(strtol(env, nullptr, 10));
    }
    else {
        zconfig_t* config = zconfig_load(ZCONFIG_PATH);
        if (config) {
            val = int(strtol(zconfig_get(config, "nut/polling_interval", std::to_string(val).c_str()), nullptr, 10));
            zconfig_destroy(&config);
        }
    }

    return val;
}

// This is **only** changed by the selftest code
static const char* g_shm_dir     = DEFAULT_SHM_DIR;
static size_t      g_shm_dir_len = strlen(DEFAULT_SHM_DIR);

// Build in buf the complete file path for the metric/asset
// Returns 0 if success, else <0
static int build_metric_filename(char* buf, size_t bufSize, const char* asset, const char* metric, const char* type)
{
    if( !(buf && (bufSize != 0)) // non empty
        || !(asset && (*asset))
        || !(metric && (*metric))
        || !(type && (*type))
        || strchr(asset, '/') // forbidden chars
        || strchr(asset, SEPARATOR)
        || strchr(metric, '/')
        || strchr(metric, SEPARATOR)
        || ((strlen(metric) + SEPARATOR_LEN + strlen(asset)) > NAME_MAX) // length limitation
    ) {
        return -1;
    }

    int r = snprintf(buf, bufSize, "%s/%s/%s@%s", g_shm_dir, type, metric, asset);
    return (r > 0) ? 0 : -1;
}

// Returns 0 if success, else <0
int fty_shm_write_metric(const char* asset, const char* type, const char* value, const char* unit, int ttl)
{
    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
    if (!proto) {
        return -1;
    }

    if (ttl < 0) {
        ttl = 0;
    }

    fty_proto_set_name(proto, "%s", asset);
    fty_proto_set_type(proto, "%s", type);
    fty_proto_set_unit(proto, "%s", unit);
    fty_proto_set_value(proto, "%s", value);
    fty_proto_set_ttl(proto, static_cast<uint32_t>(ttl));

    int r = fty_shm_write_metric_proto(proto);

    fty_proto_destroy(&proto);

    return (r == 0) ? 0 : -1;
}

// read metric from filename & set proto_metric
// Returns 0 if success, else <0
static int read_data_metric(const char* filename, fty_proto_t* proto)
{
    if (!(filename && proto)) {
        return -1;
    }

    // close file and returns -1
    #define RET_ERROR { if (file) { fclose(file); } return -1; }
    // End char* S at the latest '\n'
    #define END_CR(S) { if ((p = strrchr(S, '\n'))) { *p = 0; } }

    FILE* file = fopen(filename, "r");
    if (!file) {
        RET_ERROR;
    }

    struct stat st;
    if (fstat(fileno(file), &st) != 0) {
        RET_ERROR;
    }

    char buf[128];
    char *p = NULL;

    // get ttl
    {
        buf[0] = 0;
        if (!fgets(buf, sizeof(buf), file)) {
            RET_ERROR;
        }
        END_CR(buf);

        char* err = NULL;
        int ttl = int(strtol(buf, &err, 10));
        if (err != buf + TTL_LEN - 1) {
            RET_ERROR; // bad size
        }

        // ttl still valid ?
        if (ttl != 0) {
            time_t now = time(nullptr);
            if ((now - st.st_mtime) > ttl) {
                fclose(file);
                file = NULL;
                const char* env = getenv(AUTOCLEAN_ENV);
                if (!(env && streq(env, "OFF"))) {
                    remove(filename);
                }
                RET_ERROR;
            }
        }

        // set ttl
        fty_proto_set_ttl(proto, uint32_t(ttl));
    }

    // set timestamp (file last modification date)
    fty_proto_set_time(proto, uint64_t(st.st_mtim.tv_sec));

    // get unit
    {
        buf[0] = 0;
        if (!fgets(buf, sizeof(buf), file)) {
            RET_ERROR;
        }
        END_CR(buf);

        // set unit (can be "%", as for: load.default@ups-xxx)
        fty_proto_set_unit(proto, "%s", buf);
    }

    // get value
    {
        buf[0] = 0;
        if (!fgets(buf, sizeof(buf), file)) {
            RET_ERROR;
        }
        END_CR(buf);

        // set value
        fty_proto_set_value(proto, buf);
    }

    // get aux key/value attributes (optionals)
    {
        char buf2[sizeof(buf)];
        buf[0] = buf2[0] = 0;
        while (fgets(buf, sizeof(buf), file) // key line
               && fgets(buf2, sizeof(buf2), file) // value line
        ) {
            END_CR(buf);
            END_CR(buf2);

            // set aux key/value
            if (buf[0]) { // key not empty
                fty_proto_aux_insert(proto, buf, "%s", buf2);
            }

            buf[0] = buf2[0] = 0;
        }
    }

    fclose(file);
    return 0;

    #undef RET_ERROR
    #undef END_CR
}

// Read a metric, Set value & unit (optionals) on return.
// Returns 0 if success, else <0
int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit)
{
    if (value) {
        *value = NULL;
    }
    if (unit) {
        *unit = NULL;
    }

    char filename[PATH_MAX];
    int r = build_metric_filename(filename, sizeof(filename), asset, metric, FTY_SHM_METRIC_TYPE);
    if (r != 0) {
        return -1;
    }

    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);

    r = read_data_metric(filename, proto);
    if (r == 0) { // ok
        if (value) {
            *value = strdup(fty_proto_value(proto));
        }
        if (unit) {
            *unit = strdup(fty_proto_unit(proto));
        }
    }

    fty_proto_destroy(&proto);

    return (r == 0) ? 0 : -1;
}

// Returns 0 if success, else <0
static int fty_shm_read_family (const std::string& family, std::string asset, std::string type, fty::shm::shmMetrics& result)
{
    std::string family_dir{std::string(g_shm_dir) + "/" + family};

    DIR* dir = opendir(family_dir.c_str());
    if (!dir) {
        return -1;
    }

    bool success{true};
    try {
        std::regex regAsset(asset);
        std::regex regType(type);

        struct dirent* de;
        while ((de = readdir(dir))) {
            const char* delim = strchr(de->d_name, SEPARATOR);
            if (!delim) {
                continue; // not a metric file
            }

            size_t type_len = size_t(delim - de->d_name);
            if (std::regex_match(std::string(delim + 1), regAsset)
                && std::regex_match(std::string(de->d_name, type_len), regType)
            ) {
                const std::string filename{family_dir + "/" + de->d_name};

                fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
                int r = read_data_metric(filename.c_str(), proto);
                if (r == 0) {
                    fty_proto_set_name(proto, "%s", std::string(delim + 1).c_str());
                    fty_proto_set_type(proto, "%s", std::string(de->d_name, type_len).c_str());
                    result.add(proto); // proto owned by result
                }
                else {
                    fty_proto_destroy(&proto);
                }
            }
        }
    }
    catch (const std::exception& e) { // regex exceptions
        success = false;
    }

    closedir(dir);
    return success ? 0 : -1;
}

// should be called onl on unit test
// deletes the directory created with fty_shm_set_test_dir
// Returns 0 if success, else <0
int fty_shm_delete_test_dir()
{
    // avoid runtime directory deletion (allow only tests dir)
    if (streq(g_shm_dir, DEFAULT_SHM_DIR)) {
        return -2;
    }

    const std::string metric_dir{std::string(g_shm_dir) + "/" + FTY_SHM_METRIC_TYPE};

    DIR* dir = opendir(metric_dir.c_str());
    if (!dir) {
        return -1;
    }

    char path[2048];
    struct dirent* de;
    while ((de = readdir(dir))) {
        if (strchr(de->d_name, SEPARATOR)) { // metric file
            path[0] = 0;
            snprintf(path, sizeof(path), "%s/%s", metric_dir.c_str(), de->d_name);
            FILE* file = fopen(path, "r");
            if (file) {
                fclose(file);
                remove(path);
            }
        }
    }

    closedir(dir);
    dir = NULL;

    remove(metric_dir.c_str());

    int r = remove(g_shm_dir);
    return (r == 0) ? 0 : -1;
}

// ensure on return that the giver directory exist
// Returns 0 if success, else <0
int fty_shm_set_test_dir(const char* dirname)
{
    if (!dirname) {
        return -1;
    }

    DIR* dir = opendir(dirname);
    if (!dir) {
        int r = mkdir(dirname, 0777);
        if (r != 0) {
            return -1;
        }
    }
    else {
        closedir(dir);
        dir = NULL;
    }

    const std::string subdir{std::string(dirname) + "/" + std::string(FTY_SHM_METRIC_TYPE)};
    dir = opendir(subdir.c_str());
    if (!dir) {
        int r = mkdir(subdir.c_str(), 0777);
        if (r != 0) {
            return -1;
        }
    }
    else {
        closedir(dir);
        dir = NULL;
    }

    g_shm_dir = dirname;
    g_shm_dir_len = strlen(dirname);

    return 0;
}

// Write metric into file
// Returns 0 if success, else <0
static int write_data_metric(fty_proto_t* metric)
{
    if (!metric) {
        return -1;
    }

    char filename[PATH_MAX];
    int r = build_metric_filename(filename, sizeof(filename), fty_proto_name(metric), fty_proto_type(metric), FTY_SHM_METRIC_TYPE);
    if (r != 0) {
        return -1;
    }

    FILE* file = fopen(filename, "w");
    if (!file) {
        return -1;
    }

    int ttl = int(fty_proto_ttl(metric));
    if (ttl < 0) {
        ttl = 0;
    }

    // write ttl/unit/value
    const std::string fmt(std::string(TTL_FMT) + UNIT_FMT + "%s");
    fprintf(file, fmt.c_str(), ttl, fty_proto_unit(metric), fty_proto_value(metric));

    // write aux attributes (optionals)
    zhash_t* aux = fty_proto_aux(metric);
    if (aux) {
        for (void* it = zhash_first(aux); it; it = zhash_next(aux)) {
            const char* key = zhash_cursor(aux);
            const char* value = static_cast<char*>(it);
            fprintf(file, "\n%s\n%s", key, value);
        }
    }

    fty::shm::Publisher::publishMetric(metric); //mqtt-pub

    r = fclose(file);
    return (r == 0) ? 0 : -1;
}

int fty_shm_write_metric_proto(fty_proto_t* metric)
{
    return write_data_metric(metric);
}

int fty::shm::write_metric(fty_proto_t* metric)
{
    return fty_shm_write_metric_proto(metric);
}

int fty::shm::write_metric(const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl)
{
    if (asset.empty() || metric.empty() || value.empty()) {
        return -1;
    }

    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
    if (!proto) {
        return -1;
    }

    if (ttl < 0) {
        ttl = 0;
    }

    fty_proto_set_name(proto, "%s", asset.c_str());
    fty_proto_set_type(proto, "%s", metric.c_str());
    fty_proto_set_value(proto, "%s", value.c_str());
    fty_proto_set_unit(proto, "%s", unit.c_str());
    fty_proto_set_ttl(proto, static_cast<uint32_t>(ttl));

    int r = fty::shm::write_metric(proto);

    fty_proto_destroy(&proto);

    return (r == 0) ? 0 : -1;
}

int fty::shm::read_metric_value(const std::string& asset, const std::string& metric, std::string& value)
{
    char* value_ = NULL;
    int r = fty_shm_read_metric(asset.c_str(), metric.c_str(), &value_, NULL);
    if (r == 0) { // ok
        value = std::string{value_ ? value_ : ""};
    }

    if (value_) { free(value_); } // cleanup

    return (r == 0) ? 0 : -1;
}

int fty::shm::read_metric(const std::string& asset, const std::string& type, fty_proto_t** metric)
{
    if (!metric) {
        return -1;
    }
    *metric = NULL;

    char filename[PATH_MAX];
    int r = build_metric_filename(filename, sizeof(filename), asset.c_str(), type.c_str(), FTY_SHM_METRIC_TYPE);
    if (r != 0) {
        return -1;
    }

    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
    r = read_data_metric(filename, proto);
    if (r != 0) {
        fty_proto_destroy(&proto);
        return -1;
    }

    fty_proto_set_name(proto, "%s", asset.c_str());
    fty_proto_set_type(proto, "%s", type.c_str());

    *metric = proto; // proto owned
    return 0;
}

int fty::shm::read_metrics(const std::string& asset, const std::string& type, shmMetrics& result)
{
    const std::string family(FTY_SHM_METRIC_TYPE);

    if (family == "*") {
        DIR* dir = opendir(g_shm_dir);
        if (!dir) {
            return -1;
        }

        struct dirent* de;
        while ((de = readdir(dir))) {
            fty_shm_read_family(de->d_name, asset, type, result);
        }

        closedir(dir);
    }
    else {
        fty_shm_read_family(family, asset, type, result);
    }

    return 0;
}

fty::shm::shmMetrics::~shmMetrics()
{
    for (auto it = m_metricsVector.begin(); it != m_metricsVector.end(); ++it) {
        fty_proto_destroy(&(*it));
    }

    m_metricsVector.clear();
}

fty_proto_t* fty::shm::shmMetrics::get(int i)
{
    return m_metricsVector.at(size_t(i));
}

fty_proto_t* fty::shm::shmMetrics::getDup(int i)
{
    return fty_proto_dup(m_metricsVector.at(size_t(i)));
}

long unsigned int fty::shm::shmMetrics::size()
{
    return m_metricsVector.size();
}

void fty::shm::shmMetrics::add(fty_proto_t* metric)
{
    m_metricsVector.push_back(metric);
}
