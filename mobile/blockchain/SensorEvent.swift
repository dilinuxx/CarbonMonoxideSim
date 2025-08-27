//
//  SensorEvent.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

let ALERT_THRESHOLD: Double = 35.0 // example CO ppm alert limit

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

extension SensorEvent {
    func toCompactPayload(deviceID: UInt32) -> Data {
        let timestampInt = UInt32(timestamp.timeIntervalSince1970)
        let coScaled = UInt16(COppm * 100)               // ppm * 100
        let humidityInt = UInt8(humidity)
        let temperatureInt = Int8(temperature.rounded())
        let alertFlag: UInt8 = (COppm >= ALERT_THRESHOLD) ? 1 : 0  // define your threshold

        var data = Data()
        data.append(contentsOf: withUnsafeBytes(of: deviceID.bigEndian, Array.init))
        data.append(contentsOf: withUnsafeBytes(of: timestampInt.bigEndian, Array.init))
        data.append(contentsOf: withUnsafeBytes(of: coScaled.bigEndian, Array.init))
        data.append(humidityInt)
        data.append(UInt8(bitPattern: temperatureInt))
        data.append(alertFlag)
        return data
    }
}

struct SensorAlertPayload {
    let deviceID: UInt32
    let timestamp: UInt32       // seconds since epoch
    let coLevel: UInt16         // ppm * 100
    let humidity: UInt8         // %
    let temperature: Int8       // °C
    let alertFlag: UInt8        // 0 = normal, 1 = alert

    // Decode from BLE advertisement payload
    static func decode(_ data: Data) -> SensorAlertPayload? {
        guard data.count >= 13 else { return nil }

        let deviceID = data[0..<4].reduce(0) { ($0 << 8) | UInt32($1) }
        let timestamp = data[4..<8].reduce(0) { ($0 << 8) | UInt32($1) }
        let coLevel = data[8..<10].reduce(0) { ($0 << 8) | UInt16($1) }
        let humidity = data[10]
        let temperature = Int8(bitPattern: data[11])
        let alertFlag = data[12]

        return SensorAlertPayload(
            deviceID: deviceID,
            timestamp: timestamp,
            coLevel: coLevel,
            humidity: humidity,
            temperature: temperature,
            alertFlag: alertFlag
        )
    }
}


