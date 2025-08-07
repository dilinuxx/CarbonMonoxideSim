//
//  SensorEvent.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

struct SensorEvent {
    let timestamp: Date
    let COppm: Double
    let metadata: [String: AnyCodable]

    let humidity: Double
    let temperature: Double
    let flowRate: Double
    let heaterVoltage: Double
    let sensorResistances: [Double]  // ideally 14 elements

    func toBlockData() -> [String: AnyCodable] {
        var base: [String: AnyCodable] = [
            "timestamp": AnyCodable(timestamp.timeIntervalSince1970),
            "COppm": AnyCodable(COppm),
            "humidity": AnyCodable(humidity),
            "temperature": AnyCodable(temperature),
            "flow_rate": AnyCodable(flowRate),
            "heater_voltage": AnyCodable(heaterVoltage),
            "sensor_resistances": AnyCodable(sensorResistances)
        ]
        
        // Merge metadata keys only if they don't already exist in base
        for (key, value) in metadata where base[key] == nil {
            base[key] = value
        }
        return base
    }
}
