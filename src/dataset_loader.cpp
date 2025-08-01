#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "SensorEvent.hpp"  // Your struct definition from earlier

// Helper: Trim whitespace
inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// Parse CSV row into tokens
std::vector<std::string> splitCSVLine(const std::string& line) {
    std::stringstream ss(line);
    std::vector<std::string> tokens;
    std::string item;

    while (std::getline(ss, item, ',')) {
        tokens.push_back(trim(item));
    }

    return tokens;
}

// Load Dataset 1
std::vector<SensorEvent> loadDataset1(const std::string& filepath) {
    std::ifstream file(filepath);
    std::string line;
    std::vector<SensorEvent> events;

    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        auto tokens = splitCSVLine(line);
        if (tokens.size() < 4) continue;

        SensorEvent evt{};
        evt.timestamp = std::stoll(tokens[0]);
        evt.COppm = std::stod(tokens[1]);
        evt.relativeHumidity = std::stod(tokens[2]);
        evt.temperature = std::stod(tokens[3]);
        evt.absoluteHumidity = 0;
        evt.hazardLevel = (evt.COppm > 9.0 ? "danger" : "safe");
        evt.metadata = {
            {"source", "dataset_1"},
            {"note", "simulated sensor array"}
        };
        events.push_back(evt);
    }
    return events;
}

// Load Dataset 2 (Air Quality)
std::vector<SensorEvent> loadDataset2(const std::string& filepath) {
    std::ifstream file(filepath);
    std::string line;
    std::vector<SensorEvent> events;

    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        auto tokens = splitCSVLine(line);
        if (tokens.size() < 15) continue;

        SensorEvent evt{};
        evt.timestamp = std::time(nullptr); // Placeholder
        evt.COppm = std::stod(tokens[2]);   // CO(GT)
        evt.temperature = std::stod(tokens[12]); // T
        evt.relativeHumidity = std::stod(tokens[13]); // RH
        evt.absoluteHumidity = std::stod(tokens[14]); // AH
        evt.hazardLevel = (evt.COppm > 10 ? "warning" : "safe");
        evt.metadata = {
            {"source", "dataset_2"},
            {"note", "air quality"}
        };
        events.push_back(evt);
    }
    return events;
}

// Load Dataset 3 (Turbine Emission)
std::vector<SensorEvent> loadDataset3(const std::string& filepath) {
    std::ifstream file(filepath);
    std::string line;
    std::vector<SensorEvent> events;

    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        auto tokens = splitCSVLine(line);
        if (tokens.size() < 11) continue;

        SensorEvent evt{};
        evt.timestamp = std::time(nullptr); // Placeholder
        evt.COppm = std::stod(tokens[9]);   // CO
        evt.absoluteHumidity = std::stod(tokens[2]); // AH
        evt.turbineInletTemp = std::stod(tokens[5]); // TIT
        evt.exhaustPressure = std::stod(tokens[1]);  // AP
        evt.temperature = 0;
        evt.relativeHumidity = 0;
        evt.hazardLevel = (evt.COppm > 0.5 ? "danger" : "safe");
        evt.metadata = {
            {"source", "dataset_3"},
            {"note", "turbine engine emissions"}
        };
        events.push_back(evt);
    }
    return events;
}