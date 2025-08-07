//
//  SmartContract.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

protocol SmartContract {
    func execute(event: [String: AnyCodable])
}

class COAlertContract: SmartContract {
    func execute(event: [String: AnyCodable]) {
        if let hazard = event["hazard_level"]?.value as? String, hazard == "danger" {
            triggerAlarm()
            notifyOccupants()
        }
    }

    private func triggerAlarm() {
        print("CO Alarm Triggered")
        // Add real iOS notification/alarm trigger here
    }

    private func notifyOccupants() {
        print("Notifying occupants...")
        // Send a local or remote notification
    }
}
