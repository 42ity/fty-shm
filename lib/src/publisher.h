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

#ifndef PUBLISHER_H_INCLUDED
#define PUBLISHER_H_INCLUDED

#include <fty_proto.h>
#include <string>

// publishMetric()
// returns 0 if success, else <0

int publishMetric(fty_proto_t* metric);
int publishMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl);
int publishMetric(const std::string& fileName, const std::string& value, const std::string& unit, uint32_t ttl);

#endif
