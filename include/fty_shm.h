/*  =========================================================================
    fty-shm - Metric sharing library for 42ity

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

#ifndef FTY_SHM_H_H_INCLUDED
#define FTY_SHM_H_H_INCLUDED

//  Include the project library file
#include "fty_shm_library.h"

#ifdef __cplusplus
extern "C" {
#endif
    
//currently here until it can be merge in a fty_common* lib
int fty_get_polling_interval();
// This is the basic C API of the library. It allows to store and retrieve
// individual metrics.

// Stores a single metric in shm. The metric name must be a valid filename
// (must not contain slashes and must fit within the OS limit for filename
// length). TTL is the number of seconds for which this metric is valid,
// where 0 means infinity
// Returns 0 on success. On error, returns -1 and sets errno accordingly
int fty_shm_write_metric(const char* asset, const char* metric, const char* value, const char* unit, int ttl);

int fty_shm_write_nut_metric(const char* asset, const char* metric, const char* value, int ttl);

// Retrieve a metric from shm. Caller must free the returned values.
// Returns 0 on success. On error, returns -1 and sets errno accordingly
int fty_shm_read_metric(const char* asset, const char* metric, char** value, char** unit);

// Delete all metrics associated with this asset from shm
int fty_shm_delete_asset(const char* asset);

// Use a custom storage directory for test purposes (the passed string must
// not be freed)
int fty_shm_set_test_dir(const char* dir);

void fty_shm_test(bool verbose);

void init_default_dir();

#ifdef __cplusplus
}
// More fancy stuff is possible with the C++ API. It is arguably not the
// cleanest API on the planet -- it signals errors via a return code and
// requires the caller to provide a container for the results instead of
// relying on RVO -- but it should be good enough for now.

#include <string>
#include <unordered_map>
#include <vector>
#include <fty_proto.h>

namespace fty {
namespace shm {
    class shmMetrics
    {
        public :
            ~shmMetrics();
            fty_proto_t* get(int index);
            fty_proto_t* getDup(int index);
            void add(fty_proto_t* metric);
            long unsigned int size();

            typedef typename std::vector<fty_proto_t*> vector_type;
            typedef typename vector_type::iterator iterator;
            typedef typename vector_type::const_iterator const_iterator;

            inline iterator begin() noexcept { return m_metricsVector.begin(); }
            inline const_iterator cbegin() const noexcept { return m_metricsVector.begin(); }
            inline iterator end() noexcept { return m_metricsVector.end(); }
            inline const_iterator cend() const noexcept { return m_metricsVector.end(); }
        private :
            std::vector<fty_proto_t*> m_metricsVector;
    };

    typedef std::vector<std::string> Assets;
    struct Metric {
        std::string value;
        std::string unit;
    };
    typedef std::unordered_map<std::string, Metric> Metrics;

    int write_nut_metric(std::string asset, std::string metric, std::string value, int ttl);

    // C++ versions of fty_shm_write_metric()
    int write_metric(fty_proto_t* metric);
    int write_metric(const std::string& asset, const std::string& metric, const std::string& value, const std::string& unit, int ttl);
    inline int write_metric(const std::string& asset, const std::string& metric, const Metric& value, int ttl)
    {
        return write_metric(asset, metric, value.value, value.unit, ttl);
    }
    inline int write_metric(const std::string& asset, const std::string& metric, double value, const std::string& unit, int ttl)
    {
        return write_metric(asset, metric, std::to_string(value), unit, ttl);
    }
    inline int write_metric(const std::string& asset, const std::string& metric, int value, const std::string& unit, int ttl)
    {
        return write_metric(asset, metric, std::to_string(value), unit, ttl);
    }

    // C++ version of fty_shm_read_metric()
    int read_metric(const std::string& asset, const std::string& metric, std::string& value);
    int read_metric(const std::string& asset, const std::string& metric, std::string& value, std::string& unit);
    inline int read_metric(const std::string& asset, const std::string& metric, Metric& result)
    {
        return read_metric(asset, metric, result.value, result.unit);
    }

    // C++ wrapper for fty_shm_delete_asset()
    inline int delete_asset(const std::string& asset)
    {
        return fty_shm_delete_asset(asset.c_str());
    }

    // Fill the passed vector with assets known to the storage. Note that for
    // optimization purposes, the result can also include assets with expired
    // metrics. If there are no assets in the storage but the storage is
    // accessible, returns an empty list.
    // Returns 0 on success. On error, returns -1 sets errno accordingly and leaves
    // the vector intact
    //int find_assets(Assets& assets);

    // Fill the passed map with metrics stored for this asset. Returns an error
    // only if there is no valid metric for this asset.
    // Returns 0 on success. On error, returns -1 and sets errno accordingly
    int read_asset_metrics(const std::string& asset, Metrics& metrics);

    int read_metrics(const std::string& familly, const std::string& asset, const std::string& type, shmMetrics& result);
}
}

#endif // __cplusplus

#endif // FTY_SHM_H_H_INCLUDED
