#include "Thermometer.h"

#include <cmath>

#include <DHT.h>
#include <DallasTemperature.h>
#include <OneWire.h>

thermometer::thermometer(String name, int16_t id, uint8_t address, ThermometerTypes type, uint32_t pollingIntervalMs, bool enabled)
    : component(std::move(name), id, address, enabled), pType(type), pPollingIntervalMs(pollingIntervalMs) {}

thermometer::~thermometer() = default;

void thermometer::GetInfo(String& output) const {
    component::GetInfo(output);
    output += "Type        | " + String(TypeName(Type())) + "\r\n";
    output += "PollingMs   | " + String(PollingInterval()) + "\r\n";
    output += "Available   | " + String(Available() ? "true" : "false") + "\r\n";
    output += "Temperature | " + String(Available() ? String(Temperature(), 2) : String("unavailable")) + "\r\n";
    if(HasHumidity()) output += "Humidity    | " + String(Available() ? String(Humidity(), 2) : String("unavailable")) + "\r\n";
}

const char* thermometer::TypeName(ThermometerTypes value) {
    switch(value) {
        case ThermometerTypes::Dht11: return "DHT11";
        case ThermometerTypes::Dht21: return "DHT21";
        case ThermometerTypes::Dht22: return "DHT22";
        case ThermometerTypes::Ds18b20: return "DS18B20";
        default: return "Unknown";
    }
}

bool thermometer::ParseType(const String& value, ThermometerTypes& result) {
    if(value.equalsIgnoreCase("DHT11")) result = ThermometerTypes::Dht11;
    else if(value.equalsIgnoreCase("DHT21")) result = ThermometerTypes::Dht21;
    else if(value.equalsIgnoreCase("DHT22")) result = ThermometerTypes::Dht22;
    else if(value.equalsIgnoreCase("DS18B20")) result = ThermometerTypes::Ds18b20;
    else return false;
    return true;
}

bool thermometer::DoConfigure() {
    return pPollingIntervalMs >= MINIMUM_POLLING_INTERVAL_MS;
}

bool thermometer::DoInitialize() {
    if(pType == ThermometerTypes::Ds18b20) {
        pOneWire.reset(new (std::nothrow) OneWire(Address()));
        if(!pOneWire) return false;
        pDallas.reset(new (std::nothrow) DallasTemperature(pOneWire.get()));
        if(!pDallas) return false;
        pDallas->begin();
        pDallas->setResolution(12);
        pDallas->setWaitForConversion(false);
    } else {
        uint8_t dhtType = DHT22;
        if(pType == ThermometerTypes::Dht11) dhtType = DHT11;
        else if(pType == ThermometerTypes::Dht21) dhtType = DHT21;
        pDht.reset(new (std::nothrow) DHT(Address(), dhtType));
        if(!pDht) return false;
        pDht->begin();
    }

    pLastPollAt = millis() - pPollingIntervalMs;
    if(pType == ThermometerTypes::Ds18b20 && Enabled()) BeginDs18b20Conversion(millis());
    return true;
}

void thermometer::EnabledChanged(bool enabled) {
    if(!enabled) {
        pAvailable = false;
        pConversionPending = false;
        return;
    }

    pLastPollAt = millis() - pPollingIntervalMs;
    pFailureReported = false;
    if(pType == ThermometerTypes::Ds18b20) BeginDs18b20Conversion(millis());
}

void thermometer::DoControl(uint32_t now) {
    if(pType == ThermometerTypes::Ds18b20) {
        if(pConversionPending) {
            if((uint32_t)(now - pConversionStartedAt) < DS18B20_CONVERSION_TIME_MS) return;

            pConversionPending = false;
            float temperature = NAN;
            if(ReadDs18b20(temperature)) ApplyReading(temperature, NAN);
            else ReportReadFailure();
            return;
        }

        if((uint32_t)(now - pLastPollAt) >= pPollingIntervalMs) BeginDs18b20Conversion(now);
        return;
    }

    if((uint32_t)(now - pLastPollAt) < pPollingIntervalMs) return;
    pLastPollAt = now;

    float temperature = NAN, humidity = NAN;
    if(ReadDht(temperature, humidity)) ApplyReading(temperature, humidity);
    else ReportReadFailure();
}

bool thermometer::ReadDht(float& temperature, float& humidity) {
    if(!pDht) return false;
    humidity = pDht->readHumidity();
    temperature = pDht->readTemperature();
    return std::isfinite(temperature) && std::isfinite(humidity);
}

bool thermometer::ReadDs18b20(float& temperature) {
    if(!pDallas || pDallas->getDeviceCount() == 0) return false;
    temperature = pDallas->getTempCByIndex(0);
    return std::isfinite(temperature) && temperature != DEVICE_DISCONNECTED_C && temperature >= -55.0f && temperature <= 125.0f;
}

void thermometer::BeginDs18b20Conversion(uint32_t now) {
    if(!pDallas) return;
    pDallas->requestTemperatures();
    pLastPollAt = now;
    pConversionStartedAt = now;
    pConversionPending = true;
}

void thermometer::ApplyReading(float temperature, float humidity) {
    pTemperature = temperature;
    pHumidity = HasHumidity() ? humidity : NAN;
    pAvailable = true;
    pFailureReported = false;
}

void thermometer::ReportReadFailure() {
    pAvailable = false;
    pFailureReported = true;
}
