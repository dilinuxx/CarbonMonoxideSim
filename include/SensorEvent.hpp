#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <cstdint>

// Alias for a timestamp
using Timestamp = uint64_t;

struct SensorEvent {
    Timestamp timestamp;          // Unix time in seconds
    double COppm;                 // CO concentration in ppm (from CO(GT) or CO emission columns)
    double temperature;           // Temperature (T)
    double relativeHumidity;      // RH (Relative Humidity)
    double absoluteHumidity;      // AH (Absolute Humidity)

    // Optional: for 2nd dataset
    double turbineInletTemp;      // TIT
    double exhaustPressure;       // AP or similar pressure param

    std::string hazardLevel;      // Computed hazard level (e.g., safe, warning, danger)
    nlohmann::json metadata;      // Extra info, e.g. source dataset, sensor ID

    nlohmann::json toBlockData() const {
        return {
            {"timestamp", timestamp},
            {"COppm", COppm},
            {"temperature", temperature},
            {"relativeHumidity", relativeHumidity},
            {"absoluteHumidity", absoluteHumidity},
            {"turbineInletTemp", turbineInletTemp},
            {"exhaustPressure", exhaustPressure},
            {"hazardLevel", hazardLevel},
            {"metadata", metadata}
        };
    }
};