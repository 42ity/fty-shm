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
#include <string>
#include <unordered_map>
#include <vector>

#define FTY_SHM_METRIC_TYPE "0"

// returns pooling interval in second (>=0)
// @return : current polling interval
int fty_get_polling_interval();

// set the polling interval (seconds)
// @param val : value of polling interval (>=0)
// Returns void
void fty_shm_set_default_polling_interval(int val);

// Stores a single metric in shared memory. The asset & metric name must be a valid filename
// (must not contain slashes and must fit within the OS limit for filename
// length). TTL is the number of seconds for which this metric is valid, where 0 means infinity
// @param asset : asset name (non NULL/empty)
// @param metric : metric name (non NULL/empty)
// @param value : value (non NULL/empty)
// @param unit : unit (non NULL)
// @param ttl : time to live (>=0)
// Returns 0 on success, else -1 (sets errno accordingly)
int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl);

// Stores a single metric in shared memory, using the fty_proto_t model.
// Same specifciations as fty_shm_write_metric()
// @param proto : fty_proto_t object (non NULL)
// Returns 0 on success, else -1 (sets errno accordingly)
int fty_shm_write_metric_proto(fty_proto_t* metric);

// Retrieve a metric from shared memory.
// Caller must free the returned values (value, unit).
// @param asset : asset name
// @param metric : metric name
// @param-out value : parameter to be write the value output (set to NULL if error)
// @param-out unit : parameter to be write the unit output (set to NULL if error)
// Returns : 0 on success, -1 if metric/asset doesn't exist
int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit);

// Only UT or Debug
// Use a custom storage directory for test purposes.
// Should be called only on unit test.
// Sets the current directory path
// @param dir : the dirctory (non NULL, valid)
// Returns 0 on success, -1 if input is invalid
int fty_shm_set_test_dir(const char* dir);

// Cleanup (delete metrics and sub directories) the custom storage directory
// Returns 0 on success, else <0 (for non-zero error codes listed in rmdir() document)
int fty_shm_delete_test_dir();

namespace fty::shm {

class shmMetrics
{
public:
    ~shmMetrics();

    // Get metric at index
    // @param index : index of the metric of the cached metrics
    // @return : the object of the indexed element
    // Caller **must not** delete the returned object
    fty_proto_t* get(int index);

    // Get metric at index (duplicate)
    // @param index : index of the metric of the cached metrics
    // @return : the duplicated object of the indexed element
    // Caller **must** delete the returned object
    fty_proto_t* getDup(int index);

    // Pushes back the fty_proto_t object in the cached metrics
    // @param metric : the object
    // Caller **must not** delete the object (owned by the cache)
    void add(fty_proto_t* metric);

    // returns the size of the cached metrics
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

// Write a metric in the shared memory
// @param metric : metric object (non NULL)
// Return : 0 on success, else -1
int write_metric(fty_proto_t* metric);

// Write a metric in the shared memory
// @param asset : asset name
// @param metric : metric name
// @param value : metric value
// @param unit : metric unit (can be empty)
// @param ttl : data sotrage duration (seconds)
// Return : 0 on success, else -1
int write_metric(const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl);

// Reads the metric according to asset name and metric type. Set value on return.
// @param asset : asset name
// @param metric : metric type
// @param-out value : the value
// Return : 0 on success, else -1
int read_metric_value(const std::string& asset, const std::string& metric, std::string& value);

// Reads the metric according to asset name and metric type. Set fty_proto_t object on return on return.
// @param asset : asset name
// @param metric : metric type
// @param-out proto_metric : the fty_proto_t object
// Return : 0 on success (set proto_metric), else -1
// Caller **must** delete the returned object
int read_metric(const std::string& asset, const std::string& metric, fty_proto_t** proto_metric);

// Reads all the metrics matching to asset name and metric type. Set result on return.
// Return : 0 on success (set result), else -1
int read_metrics(const std::string& asset, const std::string& metric, shmMetrics& result);

} // namespace fty::shm
