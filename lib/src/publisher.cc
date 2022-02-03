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

#include "publisher.h"

#include <fty_common.h>
#include <fty_proto.h>
#include <fty_log.h>
#include <cxxtools/jsonserializer.h>

#include <fty/messagebus/MessageBus.h>
#include <fty/messagebus/Message.h>
#include <fty/messagebus/mqtt/MessageBusMqtt.h>

#include <fty/expected.h>

using namespace fty::messagebus;

static int metric2JSON(fty_proto_t* metric, std::string& json);
static fty_proto_t* protoMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl);

namespace fty::shm
{
    Publisher::Publisher()
    { 
        //Create the bus object
        msgBus = std::make_shared<mqtt::MessageBusMqtt>("fty-shm");

        if(msgBus != nullptr) {
            //Connect to the bus
            fty::Expected<void> connectionRet = msgBus->connect();
            if(! connectionRet) {
                logError("Error while connecting to mqtt bus {}", connectionRet.error());
                msgBus = nullptr;
            }
        } else {
            logError("Error while creating mqtt client");
        }  
    }

    int Publisher::publishMetric(fty_proto_t* metric)
    {
       // build metric json payload
        std::string json;
        int r = metric2JSON(metric, json);
        if (r != 0) return -1;

        // publish on metric topic
        // see https://confluence-prod.tcc.etn.com/display/BiosWiki/MQTT+on+IPM2
        std::string assetStr{fty_proto_name(metric)};
        std::string metricStr{fty_proto_type(metric)};

        //Build the message to send
        Message msg = Message::buildMessage(
            "fty-shm",
            "/etn/metrics/" + assetStr + "/" + metricStr,
            "MESSAGE",
            json);

        //Send the message
        if(getInstance().msgBus) {
            fty::Expected<void> sendRet = getInstance().msgBus->send(msg);
            if(!sendRet) {
                logError("Error while sending {}", sendRet.error());
                return -2;
            }
        } else {
            //logWarn("Mqtt is not connected");
        }


        return 0;
    }

    int Publisher::publishMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl)
    {
        fty_proto_t* proto = protoMetric(metric, asset, value, unit, ttl);
        int r = publishMetric(proto);
        fty_proto_destroy(&proto);
        return r;
    }

    int Publisher::publishMetric(const std::string& fileName, const std::string& value, const std::string& unit, uint32_t ttl)
    {
        // extract metric/asset from fileName (path/to/file/metric@asset)
        std::string metric{fileName};
        std::string asset;

        auto pos = metric.rfind("/");
        if (pos != std::string::npos) {
            metric = metric.substr(pos + 1);
        }
        pos = metric.find("@");
        if (pos != std::string::npos) {
            asset = metric.substr(pos + 1);
            metric = metric.substr(0, pos);
        }

        return publishMetric(metric, asset, value, unit, ttl);
    }

    Publisher& Publisher::getInstance()
    {
        static Publisher instance; // Guaranteed to be destroyed.
        return instance;
    }
}

// proto metric json serializer
// returns 0 if success, else <0
static int metric2JSON(fty_proto_t* metric, std::string& json)
{
    json.clear();

    try {
        if (!metric)
            { throw std::runtime_error("metric is NULL"); }
        if (fty_proto_id(metric) != FTY_PROTO_METRIC)
            { throw std::runtime_error("metric is not FTY_PROTO_METRIC"); }

        const char* type_ = fty_proto_type(metric); // metric
        const char* name_ = fty_proto_name(metric); // asset name
        const char* value_ = fty_proto_value(metric);
        const char* unit_ = fty_proto_unit(metric);
        const uint32_t ttl_ = fty_proto_ttl(metric);

        std::string metricName{std::string(type_ ? type_ : "null") + "@" + std::string(name_ ? name_ : "null")};
        std::string value{value_ ? value_ : ""};
        std::string unit{unit_ ? unit_ : ""};
        if (unit == " ") unit = ""; // emptied if single space

        cxxtools::SerializationInfo si;
        si.addMember("metric") <<= metricName;
        si.addMember("value") <<= value;
        si.addMember("unit") <<= unit;
        si.addMember("ttl") <<= ttl_;
        si.addMember("timestamp") <<= std::to_string(std::time(nullptr)); // epoch time (now)

        json = JSON::writeToString(si, false/*beautify*/);

        if (json.empty()) { 
            throw std::runtime_error("json payload is empty");
        }
    }
    catch (const std::exception& e) {
        logError("metric json serialization failed (e: '{}')", e.what());
        return -1;
    }

    return 0;
}


// proto metric builder
// returns a valid object (to be freed) if success, else NULL
static fty_proto_t* protoMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl)
{
    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
    if (!proto) return NULL;

    fty_proto_set_type(proto, "%s", metric.empty() ? "" : metric.c_str());
    fty_proto_set_name(proto, "%s", asset.empty() ? "" : asset.c_str());
    fty_proto_set_value(proto, "%s", value.empty() ? "" : value.c_str());
    fty_proto_set_unit(proto, "%s", unit.empty() ? "" : unit.c_str());
    fty_proto_set_ttl(proto, ttl);
    return proto;
}

