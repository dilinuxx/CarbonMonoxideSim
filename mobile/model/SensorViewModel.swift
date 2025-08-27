//
//  SensorViewModel.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import Foundation
import SwiftUI
import Combine

class SensorViewModel: ObservableObject {
    @Published var temperature: Double = 0.0
    @Published var humidity: Double = 0.0
    @Published var voltage: Double = 0.0
    @Published var classification: String = "Pending"
    @Published var predictedCOppm: Double? = nil  // Make optional
    
    @Published var graphData: [(Double, Double, Double)] = []

    func update(from event: SensorEvent) {
        // Update sensor values
        self.temperature = event.temperature
        self.humidity = event.humidity
        self.voltage = event.heaterVoltage

        // Read classification from metadata
        if let classificationValue = event.metadata["classification"]?.value as? String {
            self.classification = classificationValue.capitalized
        } else {
            self.classification = "Pending"
        }
        
        // Read predicted CO ppm (regression output) from metadata
        if let predictedCOValue = event.metadata["predicted_CO_ppm"]?.value as? Double {
            self.predictedCOppm = predictedCOValue
        } else {
            self.predictedCOppm = nil
        }

        // Append to graph data with history limit
        graphData.append((temperature, humidity, voltage))
        if graphData.count > 50 {
            graphData.removeFirst()
        }
    }
}
