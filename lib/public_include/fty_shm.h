/*  =========================================================================
    fty-shm - Metric sharing library for 42ity

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

#pragma once

#include <fty_proto.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FTY_SHM_METRIC_TYPE "0"

// currently here until it can be merge in a fty_common* lib
// @return : current polling interval
int  fty_get_polling_interval();

// @param val : value to set polling interval
void fty_shm_set_default_polling_interval(int val);

// This is the basic C API of the library. It allows to store and retrieve
// individual metrics.

// Stores a single metric in shm. The metric name must be a valid filename
// (must not contain slashes and must fit within the OS limit for filename
// length). TTL is the number of seconds for which this metric is valid,
// where 0 means infinity
// Returns 0 on success. On error, returns -1 and sets errno accordingly
int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl);

int fty_shm_write_metric_proto(fty_proto_t* metric);

// Retrieve a metric from shm. Caller must free the returned values.
// Returns 0 on success. On error, returns -1 and sets errno accordingly
// @param asset : asset name 
// @param metric : metric name
// @param value : parameter to be write the value output
// @param unit : parameter to be write the unit output
// @return : 1 on succes, -1 if asset doesn't exist  
int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit);

// Use a custom storage directory for test purposes (the passed string must
// not be freed)
// should be called only on unit test 
// sets the current folder path to test directory 
// returns 0 on succes 
// returns -1 if input string size is longer than max allowed(PATH_MAX - strlen("/") - NAME_MAX)
int fty_shm_set_test_dir(const char* dir);
// Clean the custom storage directory
// @return : 0 on succes, for non-zero error codes listed in rmdir() document 
int fty_shm_delete_test_dir();

#ifdef __cplusplus
}

#include <string>
#include <unordered_map>
#include <vector>

namespace fty::shm {

class shmMetrics
{
public:
    ~shmMetrics();
    // If you use this, DO NOT DELETE the fty_proto_t. It will be take
    // care by the shmlMetrics's destructor.
    //  (same warning if you access to it using iterator)

    // @param index : index of the m_metricsVector
    // @return : the object of the indexed element
    fty_proto_t*      get(int index);

    // duplicates the index element
    // @param index : index of the m_metricsVector
    // @return : duplicated element
    fty_proto_t*      getDup(int index);

    // pushes back the input fty_proto_t pointer
    // @param metric : member will be pushed back to the vector
    void              add(fty_proto_t* metric);

    // returns size of the vector
    long unsigned int size();

    typedef typename std::vector<fty_proto_t*>   vector_type;
    typedef typename vector_type::iterator       iterator;
    typedef typename vector_type::const_iterator const_iterator;

    inline iterator begin() noexcept
    {
        return m_metricsVector.begin();
    }
    inline const_iterator cbegin() const noexcept
    {
        return m_metricsVector.begin();
    }
    inline iterator end() noexcept
    {
        return m_metricsVector.end();
    }
    inline const_iterator cend() const noexcept
    {
        return m_metricsVector.end();
    }

private:
    std::vector<fty_proto_t*> m_metricsVector;
};

// C++ versions of fty_shm_write_metric_proto()
// @param metric : metric data will be written to file
// @return : 0 on succes, -1 if file cannot be created
int write_metric(fty_proto_t* metric);

// @param asset : asset name
// @param metric : metric name
// @param value : metric value
// @param unit : metric unit
// @param ttl : data sotrage duration 
// @return : 0 on succes, -1 if file name is incompatible
int write_metric(
    const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl);

// C++ version of fty_shm_read_metric()
// reads the metric according to asset name and type and returns the value 
// @param asset : asset name
// @param metric : metric type
// @param value : output value
// @return : 0 on succes, -1 if metric doesn't exist or cannot be opened
int read_metric_value(const std::string& asset, const std::string& metric, std::string& value);

// if return = 0 : create a fty_proto which correspond to the metric. Must be
// free by the caller.
int read_metric(const std::string& asset, const std::string& metric, fty_proto_t** proto_metric);

// on success : fill result with the metrics still valid who matches the asset
// and metric filters.
int read_metrics(const std::string& asset, const std::string& metric, shmMetrics& result);

} // namespace fty::shm

#endif // __cplusplus
