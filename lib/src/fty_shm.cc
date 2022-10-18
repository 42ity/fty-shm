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
#include <assert.h>
#include <regex>
#include "publisher.h"
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

using namespace fty::shm;

void fty_shm_set_default_polling_interval(int val)
{
    std::string s = std::to_string(val);
    setenv(POLL_ENV, s.c_str(), 1);
}

int fty_get_polling_interval()
{
    int val  = 30;
    char*      data = getenv(POLL_ENV);
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
static const char* shm_dir     = DEFAULT_SHM_DIR;
static size_t      shm_dir_len = strlen(DEFAULT_SHM_DIR);

// creates file name 
static int prepare_filename(
    char* buf, size_t bufSize, const char* asset, const char* metric, const char* type)
{
    if( (!asset || !(*asset)) || (!metric || !(*metric)) || (!type || !(*type)) )
        return -1;

    size_t assetLen = strlen(asset);
    size_t metricLen = strlen(metric);

    if (metricLen + SEPARATOR_LEN + assetLen > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (memchr(asset, '/', assetLen) || memchr(asset, SEPARATOR, assetLen) || memchr(metric, '/', metricLen) ||
        memchr(metric, SEPARATOR, metricLen)) {
        errno = EINVAL;
        return -1;
    }

    if(snprintf(buf, bufSize, "%s/%s/%s@%s",shm_dir, type, metric, asset) < 0)
        return -1;
    
    return 0;
}

int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl)
{
    fty_proto_t * proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    int result;

    if(!proto_metric)
        return -1;

    fty_proto_set_name(proto_metric, "%s", asset);
    fty_proto_set_type(proto_metric, "%s", metric);
    fty_proto_set_unit(proto_metric, "%s", unit);
    fty_proto_set_value(proto_metric, "%s", value);

    if(ttl < 0)
        ttl = 0;

    fty_proto_set_ttl(proto_metric, static_cast<uint32_t>(ttl));


    result = fty_shm_write_metric_proto(proto_metric);

    fty_proto_destroy(&proto_metric);

    return result;
}

// parses ttl from metric
// @param ttl_str : ttl line of the metric
// @param ttl : result reference
// @return : 0 on succes, -1 if the size is not appropriate
static int parse_ttl(char* ttl_str, time_t& ttl)
{
    char* err;
    int   res;

    // Delete the '\n'
    int len = int(strlen(ttl_str) - 1);
    if (ttl_str[len] == '\n')
        ttl_str[len] = '\0';
    res = int(strtol(ttl_str, &err, 10));
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }
    ttl = res;
    return 0;
}

// returns 0 on succes and fails if file doesn't exist 
// passes results to proto_metric
int read_data_metric(const char* filename, fty_proto_t* proto_metric)
{
    struct stat st;
    FILE*       file = nullptr;
    char        buf[128];
    char        bufVal[128];
    time_t      now, ttl;
    int         len;

    file = fopen(filename, "r");

    if (file == nullptr)
        return -1;

    if (fstat(fileno(file), &st) < 0)
    {
        fclose(file);
        return -1;
    }

    // get ttl
    fgets(buf, sizeof(buf), file);

    if (parse_ttl(buf, ttl) < 0)
    {
        fclose(file);
        return -1;
    }

    // data still valid ?
    if (ttl) {
        now = time(nullptr);
        if (now - st.st_mtime > ttl) {
            errno = ESTALE;
            fclose(file);
            char* valenv = getenv(AUTOCLEAN_ENV);
            if (!valenv || strcmp(valenv, "OFF") != 0)
                remove(filename);
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
    if (buf[len] == '\n')
        buf[len] = '\0';
    fty_proto_set_unit(proto_metric, "%s", buf); // unit can be "%" (ex.: load.default@ups-xxx)

    // get value
    fgets(buf, sizeof(buf), file);
    // Delete the '\n'
    len = int(strlen(buf) - 1);
    if (buf[len] == '\n')
        buf[len] = '\0';

    fty_proto_set_value(proto_metric, buf);

    while (fgets(buf, sizeof(buf), file) != nullptr) {
        if (fgets(bufVal, sizeof(bufVal), file) == nullptr) {
            break;
        }

        // Delete the '\n'
        len = int(strlen(buf) - 1);
        if (buf[len] == '\n')
            buf[len] = '\0';

        len = int(strlen(bufVal) - 1);
        if (bufVal[len] == '\n')
            bufVal[len] = '\0';

        fty_proto_aux_insert(proto_metric, buf, "%s", bufVal);
    }
    fclose(file);
    return 0;
}

int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit)
{
    char filename[PATH_MAX];
    fty_proto_t * proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    int result;

    if (prepare_filename(filename, sizeof filename, asset, metric, FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    

    result = read_data_metric(filename, proto_metric);

    if(result == 0)
    {
        strncpy(*value, fty_proto_value(proto_metric), strlen(fty_proto_value(proto_metric)));

        if (unit) {
            strncpy(*unit, fty_proto_unit(proto_metric), strlen(fty_proto_unit(proto_metric)));
        }  
    }
    

    fty_proto_destroy(&proto_metric);

    return result;
}

int fty_shm_read_family(const char* family, std::string asset, std::string type, fty::shm::shmMetrics& result)
{
    std::string family_dir = shm_dir;
    family_dir.append("/");
    family_dir.append(family);
    DIR* dir;
    if (!(dir = opendir(family_dir.c_str())))
        return -1;

    try {

        struct dirent* de;
        std::regex regType(type);
        std::regex regAsset(asset);
        while ((de = readdir(dir))) {
            const char* delim = strchr(de->d_name, SEPARATOR);
            // If not a valid metric
            if (!delim)
                continue;
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
                } else {
                    fty_proto_destroy(&proto_metric);
                }
            }
        }
    } catch (const std::regex_error& e) {
        closedir(dir);
        return -1;
    }
    closedir(dir);
    return 0;
}

int fty::shm::read_metrics(const std::string& asset, const std::string& type, shmMetrics& result)
{
    std::string family(FTY_SHM_METRIC_TYPE);
    if (family == "*") {
        DIR*           dir;
        struct dirent* de_root;
        if (!(dir = opendir(shm_dir)))
            return -1;
        dirfd(dir);
        while ((de_root = readdir(dir))) {
            fty_shm_read_family(de_root->d_name, asset, type, result);
        }
    } else {
        fty_shm_read_family(family.c_str(), asset, type, result);
    }
    return 0;
}


// should be called onl on unit test 
// deletes the folder created with fty_shm_set_test_dir
int fty_shm_delete_test_dir()
{
    if (strcmp(shm_dir, DEFAULT_SHM_DIR) == 0)
        return -2;

    struct dirent* entry = nullptr;
    DIR*           dir   = nullptr;
    std::string    metric_dir(shm_dir);
    metric_dir.append("/").append(FTY_SHM_METRIC_TYPE);
    dir = opendir(metric_dir.c_str());

    entry = readdir(dir);
    while (entry != nullptr) {
        FILE* file = nullptr;
        if (strstr(entry->d_name, "@") != nullptr) {
            char abs_path[2048] = {0};
            snprintf(abs_path, sizeof(abs_path), "%s/%s", metric_dir.c_str(), entry->d_name);
            file = fopen(abs_path, "r");
            if (file != nullptr) {
                fclose(file);
                remove(abs_path);
            }
        }
        entry = readdir(dir);
    }
    closedir(dir);
    remove(metric_dir.c_str());
    return remove(shm_dir);
}

int fty_shm_set_test_dir(const char* dir)
{
    int ret = 0;
    if (strlen(dir) > PATH_MAX - strlen("/") - NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    DIR* dird;
    if (!(dird = opendir(dir)))
        ret = mkdir(dir, 0777);
    else
        closedir(dird);
    if (ret != 0)
        return ret;

    std::string subdir(dir);
    subdir.append("/").append(FTY_SHM_METRIC_TYPE);
    if (!(dird = opendir(subdir.c_str())))
        ret = mkdir(subdir.c_str(), 0777);
    else
        closedir(dird);

    if (ret != 0)
        return ret;
    shm_dir     = dir;
    shm_dir_len = strlen(dir);
    return 0;
}

// Write ttl and value to filename
static int write_metric_data(const char* filename, fty_proto_t* metric)
{
    FILE* file = fopen(filename, "w");
    if (file == nullptr)
        return -1;
    int ttl = int(fty_proto_ttl(metric));
    if (ttl < 0)
        ttl = 0;

    std::string fmt(TTL_FMT);
    fmt.append(UNIT_FMT).append("%s");
    fprintf(file, fmt.c_str(), ttl, fty_proto_unit(metric), fty_proto_value(metric));
    zhash_t* aux = fty_proto_aux(metric);

    if (aux) {
        char* item = static_cast<char*>(zhash_first(aux));
        while (item) {
            fprintf(file, "\n%s\n%s", zhash_cursor(aux), item);
            item = static_cast<char*>(zhash_next(aux));
        }
    }
    if (fclose(file) < 0)
        return -1;

    Publisher::publishMetric(metric); //mqtt-pub
    return 0;
}

int fty_shm_write_metric_proto(fty_proto_t* metric)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, sizeof(filename), fty_proto_name(metric), fty_proto_type(metric), FTY_SHM_METRIC_TYPE) < 0)
        return -1;

    return write_metric_data(filename, metric);
}

int fty::shm::write_metric(fty_proto_t* metric)
{
    char filename[PATH_MAX];

    if(metric == nullptr)
        return -1;
    
    if (prepare_filename(filename, sizeof(filename), fty_proto_name(metric), fty_proto_type(metric), FTY_SHM_METRIC_TYPE) < 0)
        return -1;

    return write_metric_data(filename, metric);
}

int fty::shm::write_metric(
    const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl)
{
    int result;
    fty_proto_t * proto_metric = fty_proto_new(FTY_PROTO_METRIC);

    if( asset.empty() || metric.empty() || value.empty() )
        return -1;

    fty_proto_set_name(proto_metric, "%s", asset.c_str());
    fty_proto_set_type(proto_metric, "%s", metric.c_str());
    fty_proto_set_value(proto_metric, "%s", value.c_str());
    fty_proto_set_unit(proto_metric, "%s", unit.c_str());
    
    if(ttl < 0)
        ttl = 0;

    fty_proto_set_ttl(proto_metric, static_cast<uint32_t>(ttl));

    result = fty::shm::write_metric(proto_metric);
    fty_proto_destroy(&proto_metric);

    return result;
}

int fty::shm::read_metric_value(const std::string& asset, const std::string& metric, std::string& value)
{
    char        filename[PATH_MAX];
    fty_proto_t * proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    int result;

    if (prepare_filename(
            filename, sizeof(filename), asset.c_str(), metric.c_str(), FTY_SHM_METRIC_TYPE) < 0)
        return -1;

    result = read_data_metric(filename, proto_metric);
    
    if(result == 0)
        value = std::string(fty_proto_value(proto_metric));

    fty_proto_destroy(&proto_metric);
    
    return result;
}

int fty::shm::read_metric(const std::string& asset, const std::string& metric, fty_proto_t** proto_metric)
{
    char filename[PATH_MAX];
    
    if (proto_metric == nullptr) {
        return -1;
    }

    if (prepare_filename(
            filename, sizeof filename, asset.c_str(), metric.c_str(), FTY_SHM_METRIC_TYPE) < 0)
        return -1;

    *proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    fty_proto_set_name(*proto_metric, "%s", asset.c_str());
    fty_proto_set_type(*proto_metric, "%s", metric.c_str());

    int ret = read_data_metric(filename, *proto_metric);
    if (ret != 0) {
        fty_proto_destroy(proto_metric);
    }

    return ret;
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
