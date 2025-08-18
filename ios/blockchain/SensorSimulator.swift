//
//  SensorSimulator.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation
import os.signpost

class SensorSimulator {
    private var timer: Timer?
    private var reader: CSVReader?
    private var blockchain: Blockchain
    private var contract: SmartContract
    private var classifier: Classifier = Classifier()
    private var regression: Regression = Regression()
    private var featureBuffer: [[Double]] = []
    private var regressionBuffer: [[Double]] = []
    //private let sequenceLength = 10
    private let sequenceLength = 30
    private var latestRegressionResult: Double?
    private var latestClassification: String = "pending"
    var eventCallback: ((SensorEvent) -> Void)?
    
    // BLE Communication
    private let blePeripheralManager = BLEPeripheralManager()
    
    // Energy Consumption
    private let logger = OSLog(subsystem: "com.cosim.project", category: .pointsOfInterest)

    init(csvURL: URL, blockchain: Blockchain, contract: SmartContract) {
        self.reader = CSVReader(csvURL: csvURL)
        self.blockchain = blockchain
        self.contract = contract
    }

    func start(interval: TimeInterval = 10.0) {
        timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { _ in
            self.readAndTrigger()
        }
    }
    
    private func readAndTrigger() {
        guard let event = reader?.readNext() else {
            print("End of sensor data.")
            timer?.invalidate()
            return
        }

        print("Sensor Event: \(event)")

        // --- Prepare full 18-feature input vector ---
        let features: [Double] = [
            event.temperature,
            event.humidity,
            event.heaterVoltage,
            event.flowRate
        ] + event.sensorResistances  // 4 + 14 = 18

        guard features.count == 18 else {
            print("Error: Expected 18 features, got \(features.count)")
            return
        }

        // --- Append to buffer ---
        regressionBuffer.append(features)

        // Keep only the latest 30 samples
        if regressionBuffer.count > sequenceLength {
            regressionBuffer.removeFirst()
        }

        // --- Run regression only when buffer is full ---
        if regressionBuffer.count == sequenceLength {
            //
            let signpostID = OSSignpostID(log: logger)
            os_signpost(.begin, log: logger, name: "CoreML Inference", signpostID: signpostID)
            let start = Date()

            if let predictedCO = regression.predict(sensorSequence: regressionBuffer) {
                latestRegressionResult = predictedCO

                let end = Date()
                let elapsedTime = end.timeIntervalSince(start) * 1000  // ms
                print("Regression CO Prediction: \(predictedCO) [\(String(format: "%.2f", elapsedTime)) ms]")
            } else {
                print("Regression prediction failed.")
                latestRegressionResult = nil
            }
            
            os_signpost(.end, log: logger, name: "CoreML Inference", signpostID: signpostID)
        } else {
            latestRegressionResult = nil
        }

        // --- Update event metadata ---
        var updatedMetadata = event.metadata
        if let coPrediction = latestRegressionResult {
            updatedMetadata["predicted_CO_ppm"] = AnyCodable(coPrediction)
        } else {
            updatedMetadata["predicted_CO_ppm"] = AnyCodable("pending")
        }

        // --- Create new SensorEvent ---
        let updatedEvent = SensorEvent(
            timestamp: event.timestamp,
            COppm: event.COppm,
            metadata: updatedMetadata,
            humidity: event.humidity,
            temperature: event.temperature,
            flowRate: event.flowRate,
            heaterVoltage: event.heaterVoltage,
            sensorResistances: event.sensorResistances
        )

        // --- Add to blockchain and execute smart contract ---
        let blockData = updatedEvent.toBlockData()
        blockchain.addBlock(data: blockData)
        contract.execute(event: blockData)
        
        //
        //blockchain.printChain()
        
        //
        propagateBLEData(event: updatedEvent)

        // --- Notify UI/listeners ---
        eventCallback?(updatedEvent)
    }
    
    func propagateBLEData(event: SensorEvent) {
        let deviceID: UInt32 = 0x12345678 // your device ID
        let payload = event.toCompactPayload(deviceID: deviceID)

        if event.COppm >= ALERT_THRESHOLD {
            blePeripheralManager.startAdvertising(with: payload)
        } else {
            blePeripheralManager.stopAdvertising()
        }
    }




    /*
    private func readAndTrigger() {
        guard let event = reader?.readNext() else {
            print("End of sensor data.")
            timer?.invalidate()
            return
        }

        print("Sensor Event: \(event)")

        // Extract features: temperature, humidity, heaterVoltage, COppm
        let features: [Double] = [
            event.temperature,
            event.humidity,
            event.heaterVoltage,
            event.COppm
        ]

        // Append new features to buffer
        featureBuffer.append(features)

        // Run classification only when we have enough data
        if featureBuffer.count == sequenceLength {
            let start = Date()
            latestClassification = classifier.classify(sensorSequence: featureBuffer)
            let end = Date()
            let elapsedTime = end.timeIntervalSince(start) * 1000  // Convert to milliseconds
            print("Classification Result: \(latestClassification) [\(String(format: "%.2f", elapsedTime)) ms]")
            
            // Slide window
            featureBuffer.removeFirst()
        } else {
            latestClassification = "pending"
        }

        // Create a new event with classification included in metadata
        var updatedMetadata = event.metadata
        updatedMetadata["classification"] = AnyCodable(latestClassification)

        let classifiedEvent = SensorEvent(
            timestamp: event.timestamp,
            COppm: event.COppm,
            metadata: updatedMetadata,
            humidity: event.humidity,
            temperature: event.temperature,
            flowRate: event.flowRate,
            heaterVoltage: event.heaterVoltage,
            sensorResistances: event.sensorResistances
        )

        let blockData = classifiedEvent.toBlockData()
        blockchain.addBlock(data: blockData)
        contract.execute(event: blockData)

        eventCallback?(classifiedEvent)
    }
    */

    func stop() {
        timer?.invalidate()
    }
}


