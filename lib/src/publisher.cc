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
#include <log4cplus/loglevel.h>
#include <cxxtools/jsonserializer.h>
#include <mosquitto.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <ctime>

// ANSI console coloring
#define ac_WHITE   "\x1b[1;39m" // bold
#define ac_YELLOW  "\x1b[1;33m"
#define ac_RED     "\x1b[1;31m"
#define ac_0       "\x1b[0m" // reset

// logger
static struct Logger {
    Ftylog* m_logger{NULL};
    const std::string m_name{"fty-shm-mqtt"};
    const std::string m_cfgFile{FTY_COMMON_LOGGING_DEFAULT_CFG};
    const bool m_verbose{true};

    Logger()
    {
        init();
    }

    ~Logger()
    {
        if (!m_logger) return;
        log(log4cplus::TRACE_LOG_LEVEL, "logger released");
        ftylog_delete(m_logger);
        m_logger = NULL;
    }

    void init()
    {
        if (m_logger) return; // once
        m_logger = ftylog_new(m_name.c_str(), m_cfgFile.c_str());
        if (!m_logger) return;

        if (m_verbose) { /*ftylog_setLogLevelTrace(m_logger);*/ ftylog_setVerboseMode(m_logger); }
        else ftylog_setLogLevelInfo(m_logger);
        log(log4cplus::TRACE_LOG_LEVEL, "logger initialized from '%s'", m_cfgFile.c_str());
    }

    void log(log4cplus::LogLevel level, const char* fmt, ...)
    {
        if (!m_logger) return;

        std::string msg;
        if (fmt) {
            char* aux = NULL;
            va_list args;
            va_start(args, fmt);
            vasprintf(&aux, fmt, args);
            va_end(args);
            if (aux)
                { msg = std::string{aux}; free(aux); }
        }

        // ANSI console coloring
        switch (level) {
            case log4cplus::INFO_LOG_LEVEL: msg = ac_WHITE + msg + ac_0; break;
            case log4cplus::WARN_LOG_LEVEL: msg = ac_YELLOW + msg + ac_0; break;
            case log4cplus::ERROR_LOG_LEVEL:
            case log4cplus::FATAL_LOG_LEVEL: msg = ac_RED + msg + ac_0; break;
            default:;
        }
        // log
        log_macro(level, m_logger, msg.c_str());
    }
} logger;

// logging
#define LogInit() logger.init()
#define LogTrace(...) logger.log(log4cplus::TRACE_LOG_LEVEL, __VA_ARGS__)
#define LogDebug(...) logger.log(log4cplus::DEBUG_LOG_LEVEL, __VA_ARGS__)
#define LogInfo(...) logger.log(log4cplus::INFO_LOG_LEVEL, __VA_ARGS__)
#define LogWarn(...) logger.log(log4cplus::WARN_LOG_LEVEL, __VA_ARGS__)
#define LogError(...) logger.log(log4cplus::ERROR_LOG_LEVEL, __VA_ARGS__)
#define LogFatal(...) logger.log(log4cplus::FATAL_LOG_LEVEL, __VA_ARGS__)

static bool mosquitto_libInitialized{false};

// mosquitto client
static struct MosquittoClient {
    // connect args
    const char* MQTT_HOST{"localhost"};
    const int MQTT_PORT{1883};
    const int MQTT_KEEPALIVE{15}; //sec
    // publish args
    const int MQTT_QOS{1};
    const bool MQTT_RETAIN{false};

    MosquittoClient()
    {
        LogInit();

        if (!mosquitto_libInitialized) {
            mosquitto_libInitialized = true;
            mosquitto_lib_init();
            LogTrace("mosq library initialized");
        }
    }

    ~MosquittoClient()
    {
        destroy();
    }

    // publish data on topic
    int publish (const std::string& topic, const std::string& data)
    {
        if (connect() != 0)
            { return -1; }

        int msgId = 0;
        int r = mosquitto_publish(m_mosq, &msgId, topic.c_str(), static_cast<int>(data.size()), data.c_str(), MQTT_QOS, MQTT_RETAIN);
        if (r != MOSQ_ERR_SUCCESS) {
            LogError("mosq publish failed (r: %d [%s], topic: '%s')", r, strerror(errno), topic.c_str());
            return -2;
        }

        LogInfo("mosq publish success (topic: '%s')", topic.c_str());
        return 0;
    }

private:
    // members
    struct mosquitto* m_mosq{NULL};
    bool m_isConnected{false};
    std::time_t m_lastConnectionTime{0}; // sec

    // client connect
    int connect()
    {
        // force reconnect (WA broker timeout disconnection)
        std::time_t now = std::time(NULL); //sec
        if (m_mosq && m_isConnected && ((m_lastConnectionTime + 10) < now)) {
            // publish don't work if we use mosquitto_reconnect()
            LogTrace("mosq force reconnect");
            destroy(); // to create a new instance
        }

        // create mosq instance if none
        if (!m_mosq) {
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "fty-shm-mqtt-%d", getpid());
            m_mosq = mosquitto_new(clientId, true/*clean_session*/, NULL);
            m_isConnected = false;
            if (!m_mosq)
                { LogError("mosq creation failed"); }
            else
                { LogDebug("mosq creation success (clientId: '%s')", clientId); }
        }

        // connect to host if none
        if (m_mosq && !m_isConnected) {
            int r = mosquitto_connect(m_mosq, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE);
            m_isConnected = (r == MOSQ_ERR_SUCCESS);
            m_lastConnectionTime = m_isConnected ? now : 0;
            if (!m_isConnected)
                { LogError("mosq connect failed (r: %d [%s], host: '%s:%d')", r, strerror(errno), MQTT_HOST, MQTT_PORT); }
            else
                { LogDebug("mosq connect success (host: '%s:%d')", MQTT_HOST, MQTT_PORT); }
        }

        return (m_mosq && m_isConnected) ? 0 : -1;
    }

    // client disconnect
    void disconnect()
    {
        if (m_mosq && m_isConnected) {
            mosquitto_disconnect(m_mosq);
            LogTrace("mosq disconnected");
        }
        m_isConnected = false;
    }

    // client destroy
    void destroy()
    {
        disconnect();

        if (m_mosq) {
            mosquitto_destroy(m_mosq);
            LogTrace("mosq destroyed");
        }
        m_mosq = NULL;
    }
} mqClient;

// proto metric json serializer
// returns 0 if success, else <0
static int metric2JSON(fty_proto_t* metric, std::string& json)
{
    json.clear();

    if (!metric)
        { LogError("metric is NULL"); return -1; }
    if (fty_proto_id(metric) != FTY_PROTO_METRIC)
        { LogError("metric is not FTY_PROTO_METRIC"); return -2; }

    const char* type_ = fty_proto_type(metric); // metric
    const char* name_ = fty_proto_name(metric); // asset name
    const char* value_ = fty_proto_value(metric);
    const char* unit_ = fty_proto_unit(metric);
    const uint32_t ttl_ = fty_proto_ttl(metric);

    try {
        std::string metricName = std::string(type_ ? type_ : "") + "@" + std::string(name_ ? name_ : "");
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
    }
    catch (const std::exception& e) {
        LogError("json serialization failed (e: %s)", e.what());
        return -3;
    }

    return 0;
}

// proto metric publisher
// returns 0 if success, else <0
static int mqPublish(fty_proto_t* metric)
{
    int ret = -1;
    do {
        if (!metric)
            { LogError("metric is NULL"); break; }

        // build metric json payload
        std::string json;
        int r = metric2JSON(metric, json);
        if (r != 0)
            { LogError("metric2JSON failed (r: %d)", r); break; }
        if (json.empty())
            { LogError("metric2JSON json is empty"); break; }

        // publish
        std::string asset_{fty_proto_name(metric)};
        std::string metric_{fty_proto_type(metric)};
        std::string topic{"/metric/fty-shm/" + asset_ + "/" + metric_};
        r = mqClient.publish(topic, json);
        if (r != 0) break;

        ret = 0; // success
        break;
    } while(0);

    return ret;
}

// proto metric builder
// returns valid object (to be freed) if success, else NULL
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

//
// External API
//

// publishMetric()
// returns 0 if success, else <0

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
