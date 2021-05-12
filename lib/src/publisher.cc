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
#include <mosquitto.h>
#include <sys/types.h>
#include <unistd.h>

// ANSI console coloring
#define ac_WHITE   "\x1b[1;39m" // bold
#define ac_YELLOW  "\x1b[1;33m"
#define ac_RED     "\x1b[1;31m"
#define ac_0       "\x1b[0m" // reset

static int metric2JSON(fty_proto_t* metric, std::string& json)
{
    json.clear();
    if (!metric)
        { log_error("metric is NULL"); return -1; }
    if (fty_proto_id(metric) != FTY_PROTO_METRIC)
        { log_error("metric is not FTY_PROTO_METRIC"); return -2; }

    const char* type_ = fty_proto_type(metric); // metric name
    const char* name_ = fty_proto_name(metric); // asset
    const char* value_ = fty_proto_value(metric);
    const char* unit_ = fty_proto_unit(metric);

    std::string metricName = std::string(type_ ? type_ : "") + "@" + std::string(name_ ? name_ : "");
    std::string value{value_ ? value_ : ""};
    std::string unit{unit_ ? unit_ : ""};
    if (unit == " ") unit = ""; // emptied if single space

    cxxtools::SerializationInfo si;
    si.addMember("metric") <<= metricName;
    si.addMember("value") <<= value;
    si.addMember("unit") <<= unit;
    si.addMember("timestamp") <<= std::to_string(std::time(nullptr)); // epoch time (now)

    json = JSON::writeToString(si, false);
    return 0;
}

static void mqInit()
{
    static bool libInitialized = false;
    if (libInitialized) return; // once

    libInitialized = true;
    mosquitto_lib_init();

    ftylog_setInstance("fty-shm-mqtt", FTY_COMMON_LOGGING_DEFAULT_CFG);
    log_debug("mosq library initialized");
}

static int mqPublish(fty_proto_t* metric)
{
    const bool verbose = true;

    struct mosquitto* mosq = NULL;
    bool isConnected = false;
    int ret = -1;

    mqInit();

    do {
        if (!metric)
            { log_error(ac_RED "metric is NULL" ac_0); break; }

        char clientId[32];
        snprintf(clientId, sizeof(clientId), "fty-shm-mqtt-%d", getpid());

        const bool clean_session = true;
        mosq = mosquitto_new(clientId, clean_session, NULL);
        if (!mosq)
            { log_error(ac_RED "mosq creation failed" ac_0); break; }
        if (verbose)
            { log_debug("mosq creation success (clientId: %s)", clientId); }

        const char* host = "localhost"; //mqtt
        const int port = 1883;
        const int keepalive = 15;
        int r = mosquitto_connect(mosq, host, port, keepalive);
        isConnected = (r == MOSQ_ERR_SUCCESS);
        if (!isConnected)
            { log_error(ac_RED "mosq connect failed (r: %d [%s], %s:%d)" ac_0, r, strerror(errno), host, port); break; }
        if (verbose)
            { log_debug("mosq connect success (%s:%d)", host, port); }

        std::string json;
        r = metric2JSON(metric, json);
        if (r != 0)
            { log_error(ac_RED "metric2JSON failed (r: %d,)" ac_0, r); break; }
        if (json.empty())
            { log_error(ac_RED "metric2JSON json is empty" ac_0); break; }

        char topic[128];
        snprintf(topic, sizeof(topic), "/metric/fty-shm/%s/%s", fty_proto_name(metric) /*asset*/, fty_proto_type(metric));
        int msgId = 0;
        const int qos = 0;
        const bool retain = false;
        r = mosquitto_publish(mosq, &msgId, topic, static_cast<int>(json.size()), json.c_str(), qos, retain);
        if (r != MOSQ_ERR_SUCCESS)
            { log_error(ac_RED "mosq publish failed (r: %d [%s], topic: %s)" ac_0, r, strerror(errno), topic); break; }

        log_info(ac_WHITE "mosq publish (topic: %s)" ac_0, topic);
        ret = 0;
        break;
    } while(0);

    if (mosq) {
        if (isConnected) mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    return ret;
}

static fty_proto_t* protoMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl)
{
    fty_proto_t* proto = fty_proto_new(FTY_PROTO_METRIC);
    if (proto) {
        fty_proto_set_type(proto, "%s", metric.empty() ? "" : metric.c_str());
        fty_proto_set_name(proto, "%s", asset.empty() ? "" : asset.c_str());
        fty_proto_set_value(proto, "%s", value.empty() ? "" : value.c_str());
        fty_proto_set_unit(proto, "%s", unit.empty() ? "" : unit.c_str());
        fty_proto_set_ttl(proto, ttl);
    }
    return proto; // NULL or valid object
}

int publishMetric(fty_proto_t* metric)
{
    return mqPublish(metric);
}

int publishMetric(const std::string& metric, const std::string& asset, const std::string& value, const std::string& unit, uint32_t ttl)
{
    fty_proto_t* proto = protoMetric(metric, asset, value, unit, ttl);
    int r = publishMetric(proto);
    fty_proto_destroy(&proto);
    return r;
}

int publishMetric(const std::string& fileName, const std::string& value, const std::string& unit, uint32_t ttl)
{
    // extract metric/asset from fileName (path/to/file/metric@asset)
    std::string metric(fileName);
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
