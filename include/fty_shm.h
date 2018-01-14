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

// Stores a single metric in shm. The metric name must be a valid filename
// (must not contain slashes and must fit within the OS limit for filename
// length). TTL is the number of seconds for which this metric is valid,
// where 0 means infinity
// Returns 0 on success. On error, returns -1 and sets errno accordingly
int fty_shm_write_metric(const char *asset, const char *metric, const char *value, int ttl);

// Retrieve a metric from shm. Caller must free the returned value
// Returns 0 on success. On error, returns -1 and sets errno accordingly
int fty_shm_read_metric(const char *asset, const char *metric, char **value);


void fty_shm_test (bool verbose);

#ifdef __cplusplus
}
#endif

#endif // FTY_SHM_H_H_INCLUDED
