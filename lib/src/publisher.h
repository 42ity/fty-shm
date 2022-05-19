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
#pragma once

#include <fty_proto.h>
#include <string>
#include <memory>

namespace fty::messagebus2
{
        class MessageBus;
}

namespace fty::shm
{
    class Publisher
    {
    public:
        //Singleton methods
        static int publishMetric(fty_proto_t* metric);
        static int publishMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl);
        static int publishMetric(const std::string& fileName, const std::string& value, const std::string& unit, uint32_t ttl);

    private:
        Publisher();
        static Publisher& getInstance();

        std::shared_ptr<fty::messagebus2::MessageBus> msgBus;
    };
}
