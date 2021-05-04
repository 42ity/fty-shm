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

/*
@header
    fty_shm - FTY metric sharing functions
@discuss
@end
*/

#include <assert.h>
#include <string.h>
#include <regex>

#include "fty_shm.h"

#define DEFAULT_SHM_DIR "/run/42shm"

#define SEPARATOR '@'
#define SEPARATOR_LEN 1

// The first 11 bytes of each file are the ttl in 10 decimal digits, followed
// by \n.  This is a compromise between machine and human readability
#define TTL_FMT "%010d\n"
#define TTL_LEN 11

#define UNIT_FMT "%s\n"

// Convenience macros
#define FREE(x) (free(x), (x) = NULL)

void fty_shm_set_default_polling_interval(int val)
{
  std::string s = std::to_string(val);
  setenv("FTY_SHM_TEST_POLLING_INTERVAL", s.c_str(), 1);
}

int fty_get_polling_interval()
{
    static int val = 30;
    char *data = getenv("FTY_SHM_TEST_POLLING_INTERVAL");
    if(data && strtol(data, NULL, 10) > 0)
      return strtol(data,NULL, 10);
    zconfig_t *config = zconfig_load("/etc/fty-nut/fty-nut.cfg");
    if(config) {
      val = strtol(zconfig_get(config, "nut/polling_interval", std::to_string(val).c_str()), NULL, 10);
      zconfig_destroy(&config);
    }
    return val;
}

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

// Write ttl and value to filename
static int write_value(const char* filename, const char* value, const char* unit, int ttl)
{
    FILE* file = fopen(filename, "w");
    if(file == NULL)
      return -1;
    if (ttl < 0)
        ttl = 0;
    std::string fmt(TTL_FMT);
    fmt.append(UNIT_FMT).append("%s");
    fprintf(file, fmt.c_str(), ttl, unit, value);
    if(fclose(file) < 0)
      return -1;
    return 0;
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
  int ret = -1;
  struct stat st;
  FILE* file = NULL;
  char buf[128];
  time_t now, ttl;
  int len;

  file = fopen(filename, "r");
  if(file == NULL)
    return -1;
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
            fclose(file);
            char *valenv = getenv("FTY_SHM_AUTOCLEAN");
            if(!valenv || strcmp(valenv, "OFF") != 0)
              remove(filename);
            return -1;
        }
  }

  //get unit
  fgets(buf, sizeof(buf), file);
  // Delete the '\n'
  len = strlen(buf) -1;
  if(buf[len] == '\n')
    buf[len] = '\0';
  if (need_unit) {
    unit = dup_str(buf, T());
  }
  //get value
  fgets(buf, sizeof(buf), file);
  value = dup_str(buf, T());
  fclose(file);
  return 0;

shm_out_fd:
    fclose(file);
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
    return -1;
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
            fclose(file);
            char *valenv = getenv("FTY_SHM_AUTOCLEAN");
            if(!valenv || strcmp(valenv, "OFF") != 0)
              remove(filename);
            return -1;
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
  fty_proto_set_unit(proto_metric, "%s", buf); // unit can be "%" (ex.: load.default@ups-xxx)

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

    if (prepare_filename(filename, asset, strlen(asset), metric, strlen(metric), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    return write_value(filename, value, unit, ttl);
}

int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, asset, strlen(asset), metric, strlen(metric), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    if (!unit) {
        char* dummy;
        return read_value(filename, *value, dummy, false);
    }
    return read_value(filename, *value, *unit);
}

int fty_shm_read_family(const char* family, std::string asset, std::string type, fty::shm::shmMetrics& result)
{
  std::string family_dir = shm_dir;
  family_dir.append("/");
  family_dir.append(family);
  DIR* dir;
  if(!(dir = opendir(family_dir.c_str())))
    return -1;
  struct dirent* de;

  try {

    std::regex regType(type);
    std::regex regAsset(asset);
    while ((de = readdir(dir))) {
      const char* delim = strchr(de->d_name, SEPARATOR);
      //If not a valid metric
      if(!delim)
        continue;
      size_t type_name = delim - de->d_name;
      if(std::regex_match(std::string(delim+1), regAsset) && std::regex_match(std::string(de->d_name, type_name), regType)) {
        fty_proto_t *proto_metric = fty_proto_new(FTY_PROTO_METRIC);
        std::string filename(family_dir);
        filename.append("/").append(de->d_name);
        if(read_data_metric(filename.c_str(), proto_metric) == 0) {
          fty_proto_set_name(proto_metric, "%s", std::string(delim+1).c_str());
          fty_proto_set_type(proto_metric, "%s", std::string(de->d_name, type_name).c_str());
          result.add(proto_metric);
        } else {
          fty_proto_destroy(&proto_metric);
        }
      }
    }
  } catch(const std::regex_error& e) {
    closedir(dir);
    return -1;
  }
  closedir(dir);
  return 0;
}

int fty::shm::read_metrics(const std::string& asset, const std::string& type, shmMetrics& result)
{
  DIR* dir;
  std::string family(FTY_SHM_METRIC_TYPE);
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

int fty_shm_delete_test_dir()
{
  if(strcmp (shm_dir,DEFAULT_SHM_DIR) == 0)
    return -2;

  struct dirent *entry = NULL;
  DIR *dir = NULL;
  std::string metric_dir(shm_dir);
  metric_dir.append("/").append(FTY_SHM_METRIC_TYPE);
  dir = opendir(metric_dir.c_str());

  entry = readdir(dir);
  while (entry != NULL)
  {
    FILE *file = NULL;
    char abs_path[2048] = {0};
    if(strstr(entry->d_name, "@") != NULL )
    {

      sprintf(abs_path, "%s/%s", metric_dir.c_str(), entry->d_name);
      file = fopen(abs_path, "r");
      if(file != NULL)
      {
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
    if(!(dird = opendir(dir)))
      ret = mkdir(dir, 0777);
    else
      closedir(dird);
    if(ret != 0)
      return ret;

    std::string subdir(dir);
    subdir.append("/").append(FTY_SHM_METRIC_TYPE);
    if(!(dird = opendir(subdir.c_str())))
      ret = mkdir(subdir.c_str(), 0777);
    else
      closedir(dird);

    if(ret != 0)
      return ret;
    shm_dir = dir;
    shm_dir_len = strlen(dir);
    return 0;
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

    std::string fmt(TTL_FMT);
    fmt.append(UNIT_FMT).append("%s");
    fprintf(file, fmt.c_str(), ttl, fty_proto_unit(metric), fty_proto_value(metric));
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

    if (prepare_filename(filename, fty_proto_name(metric), strlen(fty_proto_name(metric)), fty_proto_type(metric), strlen(fty_proto_type(metric)), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    return write_metric_data(filename, metric);
}

int fty_shm_write_metric_proto(fty_proto_t* metric)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, fty_proto_name(metric), strlen(fty_proto_name(metric)), fty_proto_type(metric), strlen(fty_proto_type(metric)), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    return write_metric_data(filename, metric);
}

int fty::shm::write_metric(const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl)
{
    char filename[PATH_MAX];

    if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length(), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    return write_value(filename, value.c_str(), unit.c_str(), ttl);
}

int fty::shm::read_metric_value(const std::string& asset, const std::string& metric, std::string& value)
{
    char filename[PATH_MAX];
    std::string dummy;

    if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length(), FTY_SHM_METRIC_TYPE) < 0)
        return -1;
    return read_value(filename, value, dummy, false);
}

int fty::shm::read_metric(const std::string& asset, const std::string& metric, fty_proto_t **proto_metric) {
  if(proto_metric == NULL) {
    return -1;
  }

  char filename[PATH_MAX];

  if (prepare_filename(filename, asset.c_str(), asset.length(), metric.c_str(), metric.length(), FTY_SHM_METRIC_TYPE) < 0)
      return -1;

  *proto_metric = fty_proto_new(FTY_PROTO_METRIC);
  fty_proto_set_name(*proto_metric, "%s", asset.c_str());
  fty_proto_set_type(*proto_metric, "%s", metric.c_str());

  int ret = read_data_metric(filename, *proto_metric);
  if(ret != 0) {
    fty_proto_destroy(proto_metric);
  }

  return ret;
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

// test outputs directory
#define SELFTEST_RW "selftest-rw"

void fty_shm_test(bool verbose)
{
  printf(" * fty_shm_test: \n");

  std::string value;
  fty_proto_t *proto_metric, *proto_metric_result;

  assert(fty_shm_set_test_dir(SELFTEST_RW) == 0);

  assert(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 2) == 0);

  assert(fty::shm::read_metric_value("asset", "metric", value) == 0);
  assert(value == "here_is_my_value");
  printf("#1.1 Value ok\n");
  assert(fty::shm::read_metric("asset", "metric", &proto_metric) == 0);
  assert(proto_metric != NULL);
  char *result = (char*) fty_proto_name(proto_metric);
  assert (result!=NULL && streq (result, "asset"));
  result = (char*) fty_proto_type(proto_metric);
  assert (result!=NULL && streq (result, "metric"));
  assert (fty_proto_ttl(proto_metric) == 2);
  result = (char*) fty_proto_value(proto_metric);
  assert (result!=NULL && streq (result, "here_is_my_value"));
  result = (char*) fty_proto_unit(proto_metric);
  assert(result != NULL);
  printf("%s\n",result);
//  assert (result!=NULL && streq (result, "unit?"));
  printf("#1.2 fty_proto ok\n");

  //Wait the end of the data and test no more metrics
  zclock_sleep (3000);
  value = "none";
  assert(fty::shm::read_metric_value("asset", "metric", value) < 0);
  assert(value == "none");

  //test write-read proto metric (with aux)
  fty_proto_aux_insert(proto_metric, "myfirstaux", "%s", "value_first_aux");
  fty_proto_aux_insert(proto_metric, "mysecondaux", "%s", "value_second_aux");
  assert(fty::shm::write_metric(proto_metric) == 0);

  assert(fty::shm::read_metric("asset", "metric", &proto_metric_result) == 0);
  result = (char*) fty_proto_name(proto_metric_result);
  assert (result!=NULL && streq (result, "asset"));
  result = (char*) fty_proto_type(proto_metric_result);
  assert (result!=NULL && streq (result, "metric"));
  assert (fty_proto_ttl(proto_metric_result) == 2);
  result = (char*) fty_proto_value(proto_metric_result);
  assert (result!=NULL && streq (result, "here_is_my_value"));
  result = (char*) fty_proto_unit(proto_metric_result);
  //assert (result!=NULL && streq (result, "unit?"));
  result = (char*) fty_proto_aux_string(proto_metric_result, "myfirstaux", "none");
  assert (result!=NULL && streq (result, "value_first_aux"));
  result = (char*) fty_proto_aux_string(proto_metric_result, "mysecondaux", "none");
  assert (result!=NULL && streq (result, "value_second_aux"));
  fty_proto_destroy(&proto_metric_result);
  fty_proto_destroy(&proto_metric);
  printf("#2 write-read full proto : OK\n");

  zclock_sleep(3000);

  //test metric "update"
  assert(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 2) == 0);
  assert(fty::shm::write_metric("asset", "metric", "here_is_my_real_value", "unit?", 2) == 0);
  assert(fty::shm::read_metric_value("asset", "metric", value) == 0);
  assert(value == "here_is_my_real_value");
  printf("#3 update OK\n");
  //Wait the end of the data
  zclock_sleep (3000);

  //write severals metrics and test multiple read
  assert(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 5) == 0);
  assert(fty::shm::write_metric("asset2", "metric", "here_is_my_other_value", "unit?", 5) == 0);
  assert(fty::shm::write_metric("asset", "metric2", "here_is_my_value_2", "unit?", 5) == 0);
  assert(fty::shm::write_metric("asset2", "metric2", "here_is_my_other_value_2", "unit?", 5) == 0);
  assert(fty::shm::read_metric_value("asset", "metric", value) == 0);
  assert(value == "here_is_my_value");
  assert(fty::shm::read_metric_value("asset2", "metric", value) == 0);
  assert(value == "here_is_my_other_value");
  assert(fty::shm::read_metric_value("asset", "metric2", value) == 0);
  assert(value == "here_is_my_value_2");
  assert(fty::shm::read_metric_value("asset2", "metric2", value) == 0);
  assert(value == "here_is_my_other_value_2");

  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*", ".*", resultM);
    assert(resultM.size() == 4);
    for(auto &metric : resultM) {
      char* resultT = (char*) fty_proto_type(metric);
      result = (char*) fty_proto_name(metric);
      assert(result!=NULL && ((streq (result, "asset") == 0) || (streq (result, "asset2") == 0)));
      assert(resultT!=NULL && ((streq (resultT, "metric") == 0) || (streq (resultT, "metric2") == 0)));
      if(streq (result, "asset") == 0) {
        if(streq(resultT, "metric") == 0) {
          result = (char*) fty_proto_value(metric);
          assert(result!=NULL && (streq (result, "here_is_my_value") == 0));
        } else {
          result = (char*) fty_proto_value(metric);
          assert(result!=NULL && (streq (result, "here_is_my_value_2") == 0));
        }
      } else {
        if(streq(resultT, "metric2") == 0) {
          result = (char*) fty_proto_value(metric);
          assert(result!=NULL && (streq (result, "here_is_my_other_value") == 0));
        } else {
          result = (char*) fty_proto_value(metric);
          assert(result!=NULL && (streq (result, "here_is_my_other_value_2") == 0));
        }
      }
    }
  }
  printf("#4 Full read OK\n");
  //test regex
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*2", ".*", resultM);
    assert(resultM.size() == 2);
  }
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*", ".*2", resultM);
    assert(resultM.size() == 2);
  }
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics("asset", ".*", resultM);
    assert(resultM.size() == 2);
  }
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*", "metric", resultM);
    assert(resultM.size() == 2);
  }

  assert(fty::shm::write_metric("other2", "metric", "here_is_my_value", "unit?", 5) == 0);
  assert(fty::shm::write_metric("other", "metric", "here_is_my_value", "unit?", 5) == 0);
  assert(fty::shm::write_metric("other2_asset", "metric", "here_is_my_value", "unit?", 5) == 0);
  assert(fty::shm::write_metric("asset_other", "metric", "here_is_my_value", "unit?", 5) == 0);

  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics("^asset.*", ".*", resultM);
    assert(resultM.size() == 5);
  }
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*other.*", ".*", resultM);
    assert(resultM.size() == 4);
  }
  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics("(^asset|other)((?!2).)*", ".*", resultM);
    assert(resultM.size() == 4);
  }
  printf("#5 regex tests OK\n");

  //verify the autoclean
  assert(fty::shm::write_metric("long", "duration", "here_the_metric", "stand", 10) == 0);

  //get the number of "file" in the directory
  DIR *dir;
  struct dirent *ent;
  int dir_number = 0;
  std::string dir_metric(SELFTEST_RW);
  dir_metric.append("/").append(FTY_SHM_METRIC_TYPE);
  if ((dir = opendir (dir_metric.c_str())) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      dir_number++;
    }
    closedir (dir);
  } else {
    /* could not open directory */
    perror ("");
    assert(false);
  }


  //we must have file number egals to number_of_metric + 2 (. and ..)
  assert(dir_number == 11);
  //wait the expiration of some metrics
  zclock_sleep(6000);

  {
    fty::shm::shmMetrics resultM;
    fty::shm::read_metrics(".*", ".*", resultM);
    assert(resultM.size() == 1);
  }

  //get the new number of "files"
  dir_number = 0;
  if ((dir = opendir (dir_metric.c_str())) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      dir_number++;
    }
    closedir (dir);
  } else {
    /* could not open directory */
    perror ("");
    assert(false);
  }

  //only one metric file left
  assert(dir_number == 3);
  printf("#6 Autoclean : OK\n");
  fty_shm_delete_test_dir();

  printf(" * fty_shm_test: OK\n");
}
