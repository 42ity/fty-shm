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

//#define _USE_MOSQUITTO_
#define _USE_FTY_COMMON_MESSAGEBUS_

#ifdef _USE_MOSQUITTO_
    #pragma message "==== PUBLISHER _USE_MOSQUITTO_ ===="
    #include <mosquitto.h>
#else
#ifdef _USE_FTY_COMMON_MESSAGEBUS_
    #pragma message "==== PUBLISHER _USE_FTY_COMMON_MESSAGEBUS_ ===="
    #include <fty_common_messagebus.h>
#else
    #error "compile option required"
#endif
#endif //

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <list>
#include <ctime>
#include <mutex>

// ANSI console coloring
#define ac_WHITE   "\x1b[1;39m" // bold
#define ac_YELLOW  "\x1b[1;33m"
#define ac_RED     "\x1b[1;31m"
#define ac_0       "\x1b[0m" // reset

//
// logger
//

static std::mutex loggerMutex;

static struct Logger {

public:
    Logger()
    {
        init();
    }

    ~Logger()
    {
        const std::lock_guard<std::mutex> lock(loggerMutex);

        if (!m_logger) return;
        log(log4cplus::TRACE_LOG_LEVEL, "logger released");
        ftylog_delete(m_logger);
        m_logger = nullptr;
    }

    void init()
    {
        const std::lock_guard<std::mutex> lock(loggerMutex);

        if (m_logger) return; // once
        m_logger = ftylog_new(m_name.c_str(), m_cfgFile.c_str());
        if (!m_logger) return;

        if (!m_silent) {
            if (m_verbose)
                { /*ftylog_setLogLevelTrace(m_logger);*/ ftylog_setVerboseMode(m_logger); }
            else
                { ftylog_setLogLevelInfo(m_logger); }
        }

        log(log4cplus::TRACE_LOG_LEVEL, "logger initialized from '%s'", m_cfgFile.c_str());
    }

    void log(log4cplus::LogLevel level, const char* fmt, ...)
    {
        if (m_silent) return; // silent mode (no log)
        if (!m_logger) return;

        // handle variadics
        std::string msg;
        if (fmt) {
            char* aux = nullptr;
            va_list args;
            va_start(args, fmt);
            vasprintf(&aux, fmt, args);
            va_end(args);
            if (aux)
                { msg = std::string{aux}; free(aux); }
        }

        // console coloring
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

private:
    // conf
    const std::string m_name{"fty-shm-logger"};
    const std::string m_cfgFile{FTY_COMMON_LOGGING_DEFAULT_CFG};
    const bool m_silent{false}; // silent mode (no log)
    const bool m_verbose{true}; // verbosity level (trace vs. info)

    // members
    Ftylog* m_logger{nullptr};
} logger;

// logger'ing
#define LogInit() logger.init()
#define LogTrace(...) logger.log(log4cplus::TRACE_LOG_LEVEL, __VA_ARGS__)
#define LogDebug(...) logger.log(log4cplus::DEBUG_LOG_LEVEL, __VA_ARGS__)
#define LogInfo(...) logger.log(log4cplus::INFO_LOG_LEVEL, __VA_ARGS__)
#define LogWarn(...) logger.log(log4cplus::WARN_LOG_LEVEL, __VA_ARGS__)
#define LogError(...) logger.log(log4cplus::ERROR_LOG_LEVEL, __VA_ARGS__)
#define LogFatal(...) logger.log(log4cplus::FATAL_LOG_LEVEL, __VA_ARGS__)

//
// mqClient, simple mqtt client (mosquitto vs fty-common-messagebus)
//

#ifdef _USE_MOSQUITTO_

static std::mutex mosquitto_libInitMutex;
static int mosquitto_libInitCounter{0};

static struct mosquittoClient {
public:
    mosquittoClient()
    {
        LogInit();

        const std::lock_guard<std::mutex> lock(mosquitto_libInitMutex);
        if ((mosquitto_libInitCounter++) == 0) {
            int x, y, z;
            mosquitto_lib_version(&x, &y, &z);

            if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS)
                { LogError("mosq library initialization failed (%d.%d.%d)", x, y, z); }
            else
                { LogTrace("mosq library initialized (%d.%d.%d)", x, y, z); }

            LogTrace("mosq host(%s:%d), alive(%d), qos(%d), retain(%s)",
                MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE, MQTT_QOS, (MQTT_RETAIN ? "true" : "false"));
        }
    }

    ~mosquittoClient()
    {
        destroy();

        const std::lock_guard<std::mutex> lock(mosquitto_libInitMutex);
        if ((--mosquitto_libInitCounter) == 0) {
            mosquitto_lib_cleanup();
            LogTrace("mosq library cleanup");
        }
    }

    // client publish (data on topic)
    // returns 0 if success, else <0
    int publish(const std::string& topic, const std::string& data)
    {
        int r = connect();
        if (r != 0) return -1;

        std::time_t now = std::time(nullptr); // sec
        int msgId{0};
        r = mosquitto_publish(m_instance, &msgId, topic.c_str(), static_cast<int>(data.size()), data.c_str(), MQTT_QOS, MQTT_RETAIN);
        if (r != MOSQ_ERR_SUCCESS) {
            LogError("mosq publish failed (r: %d [%s], topic: '%s')", r, strerror(errno), topic.c_str());
            return -2;
        }
        m_lastActivityTime = now;

        LogInfo("mosq publish (topic: '%s')", topic.c_str());
        return 0;
    }

private:
    // connect conf.
    const char* MQTT_HOST{"localhost"};
    const int MQTT_PORT{1883};
    const int MQTT_KEEPALIVE{5}; // sec
    // publish conf.
    const int MQTT_QOS{0};
    const bool MQTT_RETAIN{true};

    // members
    struct mosquitto* m_instance{nullptr}; // client instance
    bool m_isConnected{false}; // instance con. state
    std::time_t m_lastActivityTime{0}; // instance activity timed, epoch (sec)

    // client connect
    // returns 0 if success, else <0
    int connect()
    {
        std::time_t now = std::time(nullptr); // sec

        // force reconnection to prevent server timeout disconnection
        if (m_instance && m_isConnected && ((m_lastActivityTime + MQTT_KEEPALIVE) <= now)) {
            // note: publish() do nothing if we use mosquitto_reconnect() here
            LogTrace("mosq force reconnect");
            destroy(); // brute to trigger create & connect
        }

        // create instance if none
        if (!m_instance) {
            const std::string clientId{"fty-shm-mqtt-mosq" + std::to_string(getpid())};
            const bool cleanSession{true};
            m_isConnected = false;
            m_instance = mosquitto_new(clientId.c_str(), cleanSession, nullptr);
            if (!m_instance) {
                LogError("mosq creation failed");
                return -1;
            }
            LogTrace("mosq creation (clientId: '%s')", clientId.c_str());
        }

        // connect to host if not
        if (!m_isConnected) {
            int r = mosquitto_connect(m_instance, MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE);
            m_isConnected = (r == MOSQ_ERR_SUCCESS);
            m_lastActivityTime = m_isConnected ? now : 0;
            if (!m_isConnected) {
                LogError("mosq connect failed (r: %d [%s], host: '%s:%d')", r, strerror(errno), MQTT_HOST, MQTT_PORT);
                return -2;
            }
            LogTrace("mosq connect (host: '%s:%d')", MQTT_HOST, MQTT_PORT);
        }

        return 0;
    }

    // client disconnect
    void disconnect()
    {
        if (m_instance && m_isConnected) {
            mosquitto_disconnect(m_instance);
            LogTrace("mosq disconnected");
        }
        m_isConnected = false;
        m_lastActivityTime = 0;
    }

    // client destroy
    void destroy()
    {
        disconnect();

        if (m_instance) {
            mosquitto_destroy(m_instance);
            LogTrace("mosq released");
        }
        m_instance = nullptr;
    }
} mqClient;

#endif //_USE_MOSQUITTO_

#ifdef _USE_FTY_COMMON_MESSAGEBUS_

namespace messagebus {
    class IMessageBus;
    class Message;
}

static struct ftyCommonMessagebusClient {
public:
    ftyCommonMessagebusClient()
    {
        m_instance = nullptr;
        m_isConnected = false;
        LogInit();
    }

    ~ftyCommonMessagebusClient()
    {
        destroy();
    }

    // client publish (data on topic)
    // returns 0 if success, else <0
    int publish(const std::string& topic, const std::string& data)
    {
        int r = connect();
        if (r != 0) return -1;

        try {
            messagebus::Message message;
            message.userData().push_back(data);
            message.metaData().clear();
            message.metaData().emplace(messagebus::Message::FROM, "fty-shm-mqtt-msg");
            message.metaData().emplace(messagebus::Message::SUBJECT, "pub-metric");
            m_instance->publish(topic, message);
        }
        catch (const std::exception& e) {
            LogError("mqttMsg publish failed (topic: '%s', e: '%s')", topic.c_str(), e.what());
            return -2;
        }

        LogInfo("mqttMsg publish (topic: '%s')", topic.c_str());
        return 0;
    }

private:
    // connect conf.
    const std::string MQTT_HOST{"tcp://localhost:1883"};

    // members
    messagebus::IMessageBus* m_instance; // client instance
    bool m_isConnected; // instance con. state

    // client connect
    // returns 0 if success, else <0
    int connect()
    {
        // create instance if none
        if (!m_instance) {
            const std::string clientId{"fty-shm-mqtt-msg-" + std::to_string(getpid())};
            m_isConnected = false;
            m_instance = messagebus::MqttMsgBus(MQTT_HOST, clientId);
            if (!m_instance) {
                LogError("mqttMsg creation failed (%s)", MQTT_HOST.c_str());
                return -1;
            }
            LogTrace("mqttMsg creation (host: '%s', clientId: '%s')", MQTT_HOST.c_str(), clientId.c_str());
        }

        // connect to host if not
        if (!m_isConnected) {
            try {
                m_instance->connect();
                m_isConnected = true;
            }
            catch (const std::exception& e) {
                LogError("mqttMsg connect failed (host: '%s', e: '%s')", MQTT_HOST.c_str(), e.what());
                return -2;
            }
            LogTrace("mqttMsg connect (host: '%s')", MQTT_HOST.c_str());
        }

        return 0;
    }

    // client disconnect
    void disconnect()
    {
        if (m_instance && m_isConnected) {
            LogTrace("mqttMsg disconnected (nop)");
        }
        m_isConnected = false;
    }

    // client destroy
    void destroy()
    {
        disconnect();

        if (m_instance) {
            delete m_instance;
            LogTrace("mqttMsg released");
        }
        m_instance = nullptr;
    }
} mqClient;

#endif //_USE_FTY_COMMON_MESSAGEBUS_

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

        if (json.empty())
            { throw std::runtime_error("json payload is empty"); }

        return 0;
    }
    catch (const std::exception& e) {
        LogError("metric json serialization failed (e: %s)", e.what());
    }
    return -1;
}

// proto metric mqtt publisher
// returns 0 if success, else <0
static int mqttPublish(fty_proto_t* metric)
{
    // build metric json payload
    std::string json;
    int r = metric2JSON(metric, json);
    if (r != 0) return -1;

    // publish on metric topic
    std::string asset_{fty_proto_name(metric)};
    std::string metric_{fty_proto_type(metric)};
#ifdef _USE_MOSQUITTO_
    std::string topic{"/metric/fty-shm-mosq/" + asset_ + "/" + metric_};
#endif
#ifdef _USE_FTY_COMMON_MESSAGEBUS_
    std::string topic{"/metric/fty-shm-msg/" + asset_ + "/" + metric_};
#endif
    r = mqClient.publish(topic, json);
    if (r != 0) return -2;

    return 0;
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
    return mqttPublish(metric);
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
