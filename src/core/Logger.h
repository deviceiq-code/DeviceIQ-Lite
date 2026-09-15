#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

const char* const LOG_FILE_NAME = "/device.log";
// Simpler than DeviceIQ's AppendRotating: once this is exceeded, the file
// is dropped and started fresh, rather than trimmed from the front.
const size_t LOG_FILE_MAX_SIZE = 16384;

// Same shape as DeviceIQ's logger: an Endpoints bitmask (where messages go)
// crossed with a LogLevels bitmask (which severities get through). What's
// different is the delivery itself - DeviceIQ queues messages for a
// background FreeRTOS task; the ESP8266 here has no such thing, so Write()
// just delivers synchronously, in the caller's own context.
class logger {
    public:
        enum Endpoints : uint8_t { NoLog = 0b000, Serial = 0b001, Syslog = 0b010, File = 0b100 };
        enum LogLevels : uint8_t { Error = 0b0001, Warning = 0b0010, Information = 0b0100, Debug = 0b1000, All = 0b1111 };

        logger() {}
        ~logger() {}

        inline String Syslog_Server() { return mSyslogServerHost; }
        // Mirrors DeviceIQ's SyslogServerHost(): a plain IP still works via
        // fromString() in ResolveSyslogAddress(), but a hostname is resolved
        // via DNS too - the resolved address is cached until this changes.
        inline void Syslog_Server(String Server) { if(mSyslogServerHost == Server) return; mSyslogServerHost = Server; mSyslogAddressValid = false; }
        inline uint16_t Syslog_Port() { return mSyslogPort; }
        inline void Syslog_Port(uint16_t Port) { mSyslogPort = Port; }
        inline String Hostname() { return mHostname; }
        inline void Hostname(String Value) { mHostname = Value; }
        inline uint8_t Endpoint() { return mEndpoint; }
        inline void Endpoint(uint8_t Value) { mEndpoint = Value; }
        inline uint8_t Level() { return mLevel; }
        inline void Level(uint8_t Value) { mLevel = Value; }

        void Write(const String& Message, LogLevels MessageLevel = LogLevels::Information);

    private:
        WiFiUDP mUdpClient;
        bool mUdpReady = false;

        String mSyslogServerHost = "";
        uint16_t mSyslogPort = 514;
        IPAddress mSyslogAddress;
        bool mSyslogAddressValid = false;
        String mHostname = "";
        uint8_t mEndpoint = Endpoints::Serial | Endpoints::Syslog;
        uint8_t mLevel = LogLevels::All;

        static char LevelChar(LogLevels MessageLevel);
        static String Timestamp();

        bool ResolveSyslogAddress();
        void LogToSerial(const String& Message, LogLevels MessageLevel);
        void LogToFile(const String& Message, LogLevels MessageLevel);
        void LogToSyslog(const String& Message, LogLevels MessageLevel);
};

extern logger Logger;
