#include "Logger.h"

#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <time.h>

#include "Version.h"

char logger::LevelChar(LogLevels MessageLevel) {
    switch(MessageLevel) {
        case LogLevels::Error: return 'E';
        case LogLevels::Warning: return 'W';
        case LogLevels::Debug: return 'D';
        default: return 'I';
    }
}

// Empty when the clock was never synchronized (NTP off, or boot-time sync
// failed) - callers fall back to a plain, undated line rather than
// printing a bogus 1970 timestamp.
String logger::Timestamp() {
    time_t Now = time(nullptr);
    if(Now < 100000) return "";

    struct tm TimeInfo;
    localtime_r(&Now, &TimeInfo);
    char Buf[20];
    strftime(Buf, sizeof(Buf), "%Y-%m-%d %H:%M:%S", &TimeInfo);
    return String(Buf);
}

void logger::Write(const String& Message, LogLevels MessageLevel) {
    if((mLevel & MessageLevel) == 0) return;

    if(mEndpoint & Endpoints::Serial) LogToSerial(Message, MessageLevel);
    if(mEndpoint & Endpoints::File) LogToFile(Message, MessageLevel);
    if(mEndpoint & Endpoints::Syslog) LogToSyslog(Message, MessageLevel);
}

// Endpoints::Serial and Endpoints::File are visible unqualified inside
// every member of this class (plain enum, not enum class) and shadow the
// global `Serial` object and `File` type of the same name - hence the
// explicit ::Serial / ::File below.
void logger::LogToSerial(const String& Message, LogLevels MessageLevel) {
    String Time = Timestamp();
    String Line = Time.length() ? ("[" + Time + "] [" + String(LevelChar(MessageLevel)) + "] " + Message) : ("[" + String(LevelChar(MessageLevel)) + "] " + Message);
    ::Serial.println(Line);
}

void logger::LogToFile(const String& Message, LogLevels MessageLevel) {
    String Time = Timestamp();
    String Prefix = Time.length() ? ("[" + Time + "] [") : String("[");
    String Line = Prefix + String(LevelChar(MessageLevel)) + "] " + Message + "\n";

    if(LittleFS.exists(LOG_FILE_NAME)) {
        ::File Existing = LittleFS.open(LOG_FILE_NAME, "r");
        bool TooBig = Existing && (Existing.size() + Line.length() > LOG_FILE_MAX_SIZE);
        if(Existing) Existing.close();
        if(TooBig) LittleFS.remove(LOG_FILE_NAME);
    }

    ::File LogFile = LittleFS.open(LOG_FILE_NAME, "a");
    if(!LogFile) return;
    LogFile.print(Line);
    LogFile.close();
}

void logger::LogToSyslog(const String& Message, LogLevels MessageLevel) {
    if(mSyslogServer == IPAddress(0, 0, 0, 0) || mSyslogPort == 0) return;
    if(WiFi.status() != WL_CONNECTED) return;

    if(!mUdpReady) {
        if(!mUdpClient.begin(0)) return;
        mUdpReady = true;
    }

    uint8_t Severity;
    switch(MessageLevel) {
        case LogLevels::Error: Severity = 3; break;
        case LogLevels::Warning: Severity = 4; break;
        case LogLevels::Debug: Severity = 7; break;
        default: Severity = 6; break; // Information
    }

    String TimestampUTC = "-";
    time_t Now = time(nullptr);
    if(Now >= 100000) {
        struct tm TimeInfo;
        gmtime_r(&Now, &TimeInfo);
        char Buf[21];
        strftime(Buf, sizeof(Buf), "%Y-%m-%dT%H:%M:%SZ", &TimeInfo);
        TimestampUTC = Buf;
    }

    // RFC 5424: <PRI>VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID - MSG
    String Packet;
    Packet.reserve(Message.length() + 80);
    Packet += '<';
    Packet += String(8 + Severity); // facility user = 1
    Packet += ">1 ";
    Packet += TimestampUTC;
    Packet += ' ';
    Packet += mHostname.length() ? mHostname : "-";
    Packet += " " + String(Version::ProductFamily) + " - - - ";
    Packet += Message;

    if(!mUdpClient.beginPacket(mSyslogServer, mSyslogPort)) { mUdpClient.stop(); mUdpReady = false; return; }
    mUdpClient.write((const uint8_t*)Packet.c_str(), Packet.length());
    if(!mUdpClient.endPacket()) { mUdpClient.stop(); mUdpReady = false; }
}

logger Logger;
