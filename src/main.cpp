#include "Blockchain.hpp"
#include "SmartContract.hpp"
#include "SensorSimulator.hpp"
#include "SensorEvent.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

// Utility: Convert ISO-like date to Unix timestamp
Timestamp simulateUnixTimestamp(int daysAgo = 0) {
    auto now = std::chrono::system_clock::now();
    auto simulatedTime = now - std::chrono::hours(24 * daysAgo);
    return std::chrono::duration_cast<std::chrono::seconds>(
        simulatedTime.time_since_epoch()
    ).count();
}

// Mock sensor data based on dataset pattern
std::vector<SensorEvent> generateMockEvents() {
    std::vector<SensorEvent> events;

    events.push_back(SensorEvent{
        simulateUnixTimestamp(2),  // Timestamp
        4.5,                       // COppm (above safe level)
        25.0,                      // Temperature
        45.0,                      // RH
        0.8,                       // AH
        1086.2,                    // TIT (Gas turbine)
        1018.7,                    // Pressure
        "danger",                 // Hazard level
        { {"source", "GasTurbineDataset"}, {"sensorID", "GEN01"} }
    });

    events.push_back(SensorEvent{
        simulateUnixTimestamp(1),
        1.5,
        21.5,
        50.0,
        0.75,
        0.0,
        0.0,
        "safe",
        { {"source", "AirQualityDataset"}, {"sensorID", "ENV02"} }
    });

    return events;
}

int main() {
    // Path to SQLite DB file
    const std::string dbPath = "co_blockchain.db";

    // Initialize blockchain with persistent storage
    Blockchain blockchain(dbPath);

    // Initialize smart contract
    COAlertContract contract;

    // Load mock sensor events
    std::vector<SensorEvent> events = generateMockEvents();

    // Start sensor simulator
    SensorSimulator simulator(events, blockchain, contract);
    std::cout << "Starting Sensor Simulation...\n";
    simulator.start(2); // Read every 2 seconds (can be set to 0 for immediate testing)

    // Optional: verify blockchain validity
    if (blockchain.isValidChain()) {
        std::cout << "Blockchain verified.\n";
    } else {
        std::cerr << "Blockchain is invalid.\n";
    }

    // Print all stored blocks
    std::cout << "\n--- Blockchain Data ---\n";
    blockchain.printChain();

    return 0;
}