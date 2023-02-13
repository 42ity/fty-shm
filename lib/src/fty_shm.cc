/*  =========================================================================
    fty_shm - FTY metric sharing functions

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
#include <assert.h>
#include <regex>
#include <cstring>

#define DEFAULT_SHM_DIR "/run/42shm"

#define SEPARATOR     '@'
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
    if (val < 0) {
        return;
    }
    std::string s = std::to_string(val);
    setenv(POLL_ENV, s.c_str(), 1);
}

int fty_get_polling_interval()
{
    int val  = 30;
    char* data = getenv(POLL_ENV);
    if (data && strtol(data, nullptr, 10) > 0)
        return int(strtol(data, nullptr, 10));
    zconfig_t* config = zconfig_load(ZCONFIG_PATH);
    if (config) {
        val = int(strtol(zconfig_get(config, "nut/polling_interval", std::to_string(val).c_str()), nullptr, 10));
        zconfig_destroy(&config);
    }
    return val;
}

// This is only changed by the selftest code
static const char* g_shm_dir     = DEFAULT_SHM_DIR;
static size_t      g_shm_dir_len = strlen(DEFAULT_SHM_DIR);

// Build in buf the complete file path for the metric/asset
// Returns 0 if success, else <0
static int build_metric_filename(
    char* buf, size_t bufSize, const char* asset, const char* metric, const char* type)
{
    if( (!asset || !(*asset)) || (!metric || !(*metric)) || (!type || !(*type)) ) {
        return -1;
    }

    size_t assetLen = strlen(asset);
    size_t metricLen = strlen(metric);

    if ((metricLen + SEPARATOR_LEN + assetLen) > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (memchr(asset, '/', assetLen) || memchr(asset, SEPARATOR, assetLen) || memchr(metric, '/', metricLen) ||
        memchr(metric, SEPARATOR, metricLen)) {
        errno = EINVAL;
        return -1;
    }

    if (snprintf(buf, bufSize, "%s/%s/%s@%s",g_shm_dir, type, metric, asset) < 0) {
        return -1;
    }

    return 0;
}

int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl)
{
    fty_proto_t* proto_metric = fty_proto_new(FTY_PROTO_METRIC);

    if (!proto_metric) {
        return -1;
    }
    if (ttl < 0) {
        ttl = 0;
    }

    fty_proto_set_name(proto_metric, "%s", asset);
    fty_proto_set_type(proto_metric, "%s", metric);
    fty_proto_set_unit(proto_metric, "%s", unit);
    fty_proto_set_value(proto_metric, "%s", value);
    fty_proto_set_ttl(proto_metric, static_cast<uint32_t>(ttl));

    int result = fty_shm_write_metric_proto(proto_metric);

    fty_proto_destroy(&proto_metric);

    return result;
}

// parses ttl from metric
// @param ttl_str : ttl line of the metric
// @param ttl : result reference
// @return : 0 on succes, -1 if the size is not appropriate
static int parse_ttl(char* ttl_str, time_t& ttl)
{
    // Delete the '\n'
    int len = int(strlen(ttl_str) - 1);
    if (ttl_str[len] == '\n') {
        ttl_str[len] = '\0';
    }

    char* err;
    int res = int(strtol(ttl_str, &err, 10));
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }

    ttl = res;
    return 0;
}

// assume filename is set, non NULL
// returns 0 on succes and fails if file doesn't exist
// passes results to proto_metric
static int read_data_metric(const char* filename, fty_proto_t* proto_metric)
{
    if (!proto_metric) {
        return -1;
    }

    struct stat st;
    FILE*       file = nullptr;
    char        buf[128];
    char        bufVal[128];
    time_t      now, ttl;
    int         len;

    file = fopen(filename, "r");

    if (file == nullptr) {
        return -1;
    }

    if (fstat(fileno(file), &st) < 0) {
        fclose(file);
        return -1;
    }

    // get ttl
    fgets(buf, sizeof(buf), file);

    if (parse_ttl(buf, ttl) < 0) {
        fclose(file);
        return -1;
    }

    // data still valid ?
    if (ttl) {
        now = time(nullptr);
        if ((now - st.st_mtime) > ttl) {
            errno = ESTALE;
            fclose(file);
            char* valenv = getenv(AUTOCLEAN_ENV);
            if (!valenv || strcmp(valenv, "OFF") != 0) {
                remove(filename);
            }

            return -1;
        }
    }

    // set ttl
    fty_proto_set_ttl(proto_metric, uint32_t(ttl));
    // set timestamp
    fty_proto_set_time(proto_metric, uint64_t(st.st_mtim.tv_sec));

    // get unit
    fgets(buf, sizeof(buf), file);
    // Delete the '\n'
    len = int(strlen(buf) - 1);
    if (buf[len] == '\n') {
        buf[len] = '\0';
    }
    fty_proto_set_unit(proto_metric, "%s", buf); // unit can be "%" (ex.: load.default@ups-xxx)

    // get value
    fgets(buf, sizeof(buf), file);
    // Delete the '\n'
    len = int(strlen(buf) - 1);
    if (buf[len] == '\n') {
        buf[len] = '\0';
    }

    fty_proto_set_value(proto_metric, buf);

    // read aux key/value's
    while (fgets(buf, sizeof(buf), file) != nullptr) {
        if (fgets(bufVal, sizeof(bufVal), file) == nullptr) {
            break;
        }

        // Delete the '\n'
        len = int(strlen(buf) - 1);
        if (buf[len] == '\n') {
            buf[len] = '\0';
        }

        len = int(strlen(bufVal) - 1);
        if (bufVal[len] == '\n') {
            bufVal[len] = '\0';
        }

        fty_proto_aux_insert(proto_metric, buf, "%s", bufVal);
    }

    fclose(file);

    return 0;
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
    if (build_metric_filename(filename, sizeof filename, asset, metric, FTY_SHM_METRIC_TYPE) < 0) {
        return -1;
    }

    fty_proto_t * proto_metric = fty_proto_new(FTY_PROTO_METRIC);

    int ret = read_data_metric(filename, proto_metric);
    if (ret == 0) { // ok
        if (value) {
            *value = strdup(fty_proto_value(proto_metric));
        }
        if (unit) {
            *unit = strdup(fty_proto_unit(proto_metric));
        }
    }

    fty_proto_destroy(&proto_metric);

    return ret;
}

static int fty_shm_read_family (const char* family, std::string asset, std::string type, fty::shm::shmMetrics& result)
{
    std::string family_dir = g_shm_dir;
    family_dir.append("/");
    family_dir.append(family);
    DIR* dir = opendir(family_dir.c_str());
    if (!dir) {
        return -1;
    }

    try {
        struct dirent* de;
        std::regex regType(type);
        std::regex regAsset(asset);
        while ((de = readdir(dir))) {
            const char* delim = strchr(de->d_name, SEPARATOR);
            // If not a valid metric
            if (!delim) {
                continue;
            }

            size_t type_name = size_t(delim - de->d_name);
            if (std::regex_match(std::string(delim + 1), regAsset) &&
                std::regex_match(std::string(de->d_name, type_name), regType)) {
                fty_proto_t* proto_metric = fty_proto_new(FTY_PROTO_METRIC);
                std::string  filename(family_dir);
                filename.append("/").append(de->d_name);
                if (read_data_metric(filename.c_str(), proto_metric) == 0) {
                    fty_proto_set_name(proto_metric, "%s", std::string(delim + 1).c_str());
                    fty_proto_set_type(proto_metric, "%s", std::string(de->d_name, type_name).c_str());
                    result.add(proto_metric);
                }
                else {
                    fty_proto_destroy(&proto_metric);
                }
            }
        }
    }
    catch (const std::regex_error& e) {
        closedir(dir);
        return -1;
    }

    closedir(dir);
    return 0;
}

// should be called onl on unit test
// deletes the folder created with fty_shm_set_test_dir
// Returns 0 if success, else <0
int fty_shm_delete_test_dir()
{
    // avoid runtime directory deletion (allow only tests dir)
    if (streq(g_shm_dir, DEFAULT_SHM_DIR)) {
        return -2;
    }

    std::string metric_dir(std::string(g_shm_dir) + "/" + FTY_SHM_METRIC_TYPE);

    DIR* dir = opendir(metric_dir.c_str());
    if (!dir) {
        return -1;
    }

    struct dirent* entry = readdir(dir);
    while (entry) {
        if (strstr(entry->d_name, "@")) {
            char abs_path[2048];
            snprintf(abs_path, sizeof(abs_path), "%s/%s", metric_dir.c_str(), entry->d_name);
            FILE* file = fopen(abs_path, "r");
            if (file) {
                fclose(file);
                remove(abs_path);
            }
        }
        entry = readdir(dir);
    }
    closedir(dir);
    dir = NULL;

    remove(metric_dir.c_str());

    int ret = remove(g_shm_dir);
    return ret;
}

// ensure on return that the giver directory exist
// Returns 0 if ok, else <-1
int fty_shm_set_test_dir(const char* dir)
{
    if (!dir) {
        return -1;
    }

    DIR* dird = opendir(dir);
    if (!dird) {
        int ret = mkdir(dir, 0777);
        if (ret != 0) {
            return ret;
        }
    }
    else {
        closedir(dird);
        dird = NULL;
    }

    std::string subdir(std::string(dir) + "/" + std::string(FTY_SHM_METRIC_TYPE));
    dird = opendir(subdir.c_str());
    if (!dird) {
        int ret = mkdir(subdir.c_str(), 0777);
        if (ret != 0) {
            return ret;
        }
    }
    else {
        closedir(dird);
        dird = NULL;
    }

    g_shm_dir = dir;
    g_shm_dir_len = strlen(dir);

    return 0;
}

// Write ttl and value to filename
static int write_metric_data(fty_proto_t* metric)
{
    if (!metric) {
        return -1;
    }

    char filename[PATH_MAX];
    if (build_metric_filename(filename, sizeof(filename), fty_proto_name(metric), fty_proto_type(metric), FTY_SHM_METRIC_TYPE) < 0) {
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

    std::string fmt(std::string(TTL_FMT) + UNIT_FMT + "%s");
    fprintf(file, fmt.c_str(), ttl, fty_proto_unit(metric), fty_proto_value(metric));

    zhash_t* aux = fty_proto_aux(metric);
    if (aux) {
        char* value = static_cast<char*>(zhash_first(aux));
        while (value) {
            const char* key = zhash_cursor(aux);
            fprintf(file, "\n%s\n%s", key, value);
            value = static_cast<char*>(zhash_next(aux));
        }
    }

    fty::shm::Publisher::publishMetric(metric); //mqtt-pub

    if (fclose(file) < 0) {
        return -1;
    }
    return 0;
}

int fty_shm_write_metric_proto(fty_proto_t* metric)
{
    return write_metric_data(metric);
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

    if (ttl < 0) {
        ttl = 0;
    }

    fty_proto_t *proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    if (!proto_metric) {
        return -1;
    }

    fty_proto_set_name(proto_metric, "%s", asset.c_str());
    fty_proto_set_type(proto_metric, "%s", metric.c_str());
    fty_proto_set_value(proto_metric, "%s", value.c_str());
    fty_proto_set_unit(proto_metric, "%s", unit.c_str());
    fty_proto_set_ttl(proto_metric, static_cast<uint32_t>(ttl));

    int ret = fty::shm::write_metric(proto_metric);

    fty_proto_destroy(&proto_metric);

    return ret;
}

int fty::shm::read_metric_value(const std::string& asset, const std::string& metric, std::string& value)
{
    char* value_s = NULL;
    int ret = fty_shm_read_metric(asset.c_str(), metric.c_str(), &value_s, NULL);
    if (ret == 0) { // ok
        value = value_s ? value_s : "";
    }

    // cleanup
    if (value_s) {
        free(value_s);
    }

    return ret;
}

int fty::shm::read_metric(const std::string& asset, const std::string& metric, fty_proto_t** proto_metric)
{
    if (!proto_metric) {
        return -1;
    }
    *proto_metric = NULL;

    char filename[PATH_MAX];
    if (build_metric_filename(filename, sizeof filename, asset.c_str(), metric.c_str(), FTY_SHM_METRIC_TYPE) < 0) {
        return -1;
    }

    fty_proto_t* p_metric = fty_proto_new(FTY_PROTO_METRIC);
    if (!p_metric) {
        return -1;
    }
    fty_proto_set_name(p_metric, "%s", asset.c_str());
    fty_proto_set_type(p_metric, "%s", metric.c_str());

    int ret = read_data_metric(filename, p_metric);
    if (ret == 0) {
        *proto_metric = p_metric;
        p_metric = NULL;
    }

    fty_proto_destroy(&p_metric);

    return ret;
}

int fty::shm::read_metrics(const std::string& asset, const std::string& type, shmMetrics& result)
{
    std::string family(FTY_SHM_METRIC_TYPE);
    if (family == "*") {
        DIR* dir = opendir(g_shm_dir);
        if (!dir) {
            return -1;
        }
        dirfd(dir); // helpful?!

        struct dirent* de_root;
        while ((de_root = readdir(dir))) {
            fty_shm_read_family(de_root->d_name, asset, type, result);
        }

        closedir(dir);
    }
    else {
        fty_shm_read_family(family.c_str(), asset, type, result);
    }

    return 0;
}

fty::shm::shmMetrics::~shmMetrics()
{
    for (std::vector<fty_proto_t*>::iterator i = m_metricsVector.begin(); i != m_metricsVector.end(); ++i) {
        fty_proto_destroy(&(*i));
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
