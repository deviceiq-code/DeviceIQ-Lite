#pragma once

#include <Arduino.h>
#include <memory>

#include "Component.h"

class DHT;
class DallasTemperature;
class OneWire;

// Port of DeviceIQ's thermometer component, GPIO-onboard only (no I2C/DHT12
// path - Lite has no Bus concept).
class thermometer final : public component {
    public:
        enum class ThermometerTypes : uint8_t { Dht11, Dht21, Dht22, Ds18b20 };

        static constexpr uint32_t DEFAULT_POLLING_INTERVAL_MS = 5000;
        static constexpr uint32_t MINIMUM_POLLING_INTERVAL_MS = 1000;

        thermometer(
            String name,
            int16_t id,
            uint8_t address,
            ThermometerTypes type = ThermometerTypes::Ds18b20,
            uint32_t pollingIntervalMs = DEFAULT_POLLING_INTERVAL_MS,
            bool enabled = true
        );
        ~thermometer() override;

        [[nodiscard]] Classes Class() const override { return Classes::Thermometer; }
        [[nodiscard]] ThermometerTypes Type() const { return pType; }
        [[nodiscard]] uint32_t PollingInterval() const { return pPollingIntervalMs; }
        [[nodiscard]] bool HasHumidity() const { return pType != ThermometerTypes::Ds18b20; }
        [[nodiscard]] bool Available() const { return pAvailable; }
        [[nodiscard]] float Temperature() const { return pTemperature; }
        [[nodiscard]] float Humidity() const { return pHumidity; }

        void GetInfo(String& output) const override;
        [[nodiscard]] static const char* TypeName(ThermometerTypes value);
        [[nodiscard]] static bool ParseType(const String& value, ThermometerTypes& result);

    protected:
        bool DoConfigure() override;
        bool DoInitialize() override;
        void EnabledChanged(bool enabled) override;
        void DoControl(uint32_t now) override;

    private:
        static constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

        [[nodiscard]] bool ReadDht(float& temperature, float& humidity);
        [[nodiscard]] bool ReadDs18b20(float& temperature);
        void BeginDs18b20Conversion(uint32_t now);
        void ApplyReading(float temperature, float humidity);
        void ReportReadFailure();

        const ThermometerTypes pType;
        const uint32_t pPollingIntervalMs;
        std::unique_ptr<DHT> pDht;
        std::unique_ptr<OneWire> pOneWire;
        std::unique_ptr<DallasTemperature> pDallas;

        float pTemperature = NAN;
        float pHumidity = NAN;
        bool pAvailable = false;
        uint32_t pLastPollAt = 0;
        uint32_t pConversionStartedAt = 0;
        bool pConversionPending = false;
        bool pFailureReported = false;
};
