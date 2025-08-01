#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <iostream>

// Abstract base class for all smart contracts
class SmartContract {
public:
    virtual ~SmartContract() = default;

    // Executes the contract logic based on a new blockchain event
    virtual void execute(const nlohmann::json& event) = 0;
};

// ----------------------------------------
// Carbon Monoxide Hazard Smart Contract
class COAlertContract : public SmartContract {
public:
    void execute(const nlohmann::json& event) override {
        if (event.contains("hazardLevel") && event["hazardLevel"].is_string()) {
            std::string hazard = event["hazardLevel"];
            if (hazard == "danger") {
                triggerAlarm();
                notifyOccupants();
            }
        }
    }

private:
    void triggerAlarm() {
        std::cout << "[ALERT] Carbon monoxide danger level reached. Triggering alarm..." << std::endl;
        // Implement GPIO or system-specific alarm here for embedded
    }

    void notifyOccupants() {
        std::cout << "[INFO] Notifying occupants of hazardous CO levels." << std::endl;
        // Could be extended to push mobile notifications or log to remote server
    }
};