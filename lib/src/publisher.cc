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
#include <string>

// ANSI console coloring
#define ac_WHITE   "\x1b[1;39m" // bold
#define ac_YELLOW  "\x1b[1;33m"
#define ac_RED     "\x1b[1;31m"
#define ac_0       "\x1b[0m" // reset

static Ftylog* mqLogger = NULL;

#define mqLogDebug(...) { if (mqLogger) log_debug_log(mqLogger, __VA_ARGS__); }
#define mqLogInfo(...) { if (mqLogger) log_info_log(mqLogger, __VA_ARGS__); }
#define mqLogError(...) { if (mqLogger) log_error_log(mqLogger, __VA_ARGS__); }

static void mqLoggerInit(bool verbose)
{
    if (mqLogger) return; // once

    mqLogger = ftylog_new("fty-shm-mqtt", FTY_COMMON_LOGGING_DEFAULT_CFG);
    if (!mqLogger) return;

    if (verbose) ftylog_setLogLevelTrace(mqLogger);
    else ftylog_setLogLevelInfo(mqLogger);
    mqLogDebug("mqLogger initialized");
}

static void mqClientInit()
{
    static bool libInitialized = false;
    if (libInitialized) return; // once

    libInitialized = true;
    mosquitto_lib_init();
    mqLogDebug("mosq library initialized");
}

// proto metric json serializer
static int metric2JSON(fty_proto_t* metric, std::string& json)
{
    json.clear();
    if (!metric)
        { mqLogError("metric is NULL"); return -1; }
    if (fty_proto_id(metric) != FTY_PROTO_METRIC)
        { mqLogError("metric is not FTY_PROTO_METRIC"); return -2; }

    const char* type_ = fty_proto_type(metric); // metric name
    const char* name_ = fty_proto_name(metric); // asset
    const char* value_ = fty_proto_value(metric);
    const char* unit_ = fty_proto_unit(metric);
    const uint32_t ttl = fty_proto_ttl(metric);

    try {
        std::string metricName = std::string(type_ ? type_ : "") + "@" + std::string(name_ ? name_ : "");
        std::string value{value_ ? value_ : ""};
        std::string unit{unit_ ? unit_ : ""};
        if (unit == " ") unit = ""; // emptied if single space

        cxxtools::SerializationInfo si;
        si.addMember("metric") <<= metricName;
        si.addMember("value") <<= value;
        si.addMember("unit") <<= unit;
        si.addMember("ttl") <<= ttl;
        si.addMember("timestamp") <<= std::to_string(std::time(nullptr)); // epoch time (now)

        json = JSON::writeToString(si, false/*beautify*/);
    }
    catch (const std::exception& e) {
        mqLogError("serialization failed (e: %s)", e.what());
        return -3;
    }

    return 0;
}

// proto metric builder
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

// proto metric publisher
static int mqPublish(fty_proto_t* metric)
{
    mqLoggerInit(false/*verbose*/);
    mqClientInit();

    struct mosquitto* mosq = NULL;
    bool isConnected = false;
    int ret = -1;

    do {
        if (!metric)
            { mqLogError(ac_RED "metric is NULL" ac_0); break; }

        // json payload
        std::string json;
        int r = metric2JSON(metric, json);
        if (r != 0)
            { mqLogError(ac_RED "metric2JSON failed (r: %d,)" ac_0, r); break; }
        if (json.empty())
            { mqLogError(ac_RED "metric2JSON json is empty" ac_0); break; }

        // new mosq instance
        char clientId[32];
        snprintf(clientId, sizeof(clientId), "fty-shm-mqtt-%d", getpid());
        const bool clean_session = true;
        mosq = mosquitto_new(clientId, clean_session, NULL);
        if (!mosq)
            { mqLogError(ac_RED "mosq creation failed" ac_0); break; }
        mqLogDebug("mosq creation success (clientId: %s)", clientId);

        // connect to host/broker
        const char* host = "localhost";
        const int port = 1883;
        const int keepalive = 15;
        r = mosquitto_connect(mosq, host, port, keepalive);
        isConnected = (r == MOSQ_ERR_SUCCESS);
        if (!isConnected)
            { mqLogError(ac_RED "mosq connect failed (r: %d [%s], %s:%d)" ac_0, r, strerror(errno), host, port); break; }
        mqLogDebug("mosq connect success (%s:%d)", host, port);

        // publish
        char topic[128];
        snprintf(topic, sizeof(topic), "/metric/fty-shm/%s/%s", fty_proto_name(metric) /*asset*/, fty_proto_type(metric));
        int msgId = 0;
        const int qos = 0;
        const bool retain = false;
        r = mosquitto_publish(mosq, &msgId, topic, static_cast<int>(json.size()), json.c_str(), qos, retain);
        if (r != MOSQ_ERR_SUCCESS)
            { mqLogError(ac_RED "mosq publish failed (r: %d [%s], topic: %s)" ac_0, r, strerror(errno), topic); break; }

        // success
        mqLogInfo(ac_WHITE "mosq publish success (topic: %s)" ac_0, topic);
        ret = 0;
        break;
    } while(0);

    if (mosq) {
        if (isConnected) mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    return ret;
}

//
// External API
//

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
