//
//  SensorDashboardView.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import SwiftUI

struct SensorDashboardView: View {
    @StateObject var viewModel = SensorViewModel()

    var body: some View {
        VStack(spacing: 20) {
            HStack {
                FeatureCard(icon: "thermometer", title: "Temperature", value: "\(String(format: "%.1f", viewModel.temperature))°C", color: .red)
                FeatureCard(icon: "drop.fill", title: "Humidity", value: "\(String(format: "%.1f", viewModel.humidity))%", color: .blue)
                FeatureCard(icon: "bolt.fill", title: "Voltage", value: "\(String(format: "%.2f", viewModel.voltage))V", color: .green)
            }
            
            Text("Classification: \(viewModel.classification.uppercased())")
                .font(.headline)
                .foregroundColor(colorForClassification(viewModel.classification))
                .padding()
                .frame(maxWidth: .infinity)
                .background(Color(.systemGray5))
                .cornerRadius(10)
            
            // Added predicted CO ppm display
            Text("Predicted CO: \(viewModel.predictedCOppm != nil ? String(format: "%.2f ppm", viewModel.predictedCOppm!) : "N/A")")
                .font(.subheadline)
                .foregroundColor(.primary)
                .padding(.bottom, 10)

            LineGraphView(values: viewModel.graphData)

            Spacer()
        }
        .padding()
        .onAppear {
            startSimulation()
        }
    }

    func startSimulation() {
        if let csvURL = getCSVFilePath() {
            let blockchain = Blockchain()
            let contract = COAlertContract()
            let simulator = SensorSimulator(csvURL: csvURL, blockchain: blockchain, contract: contract)

            simulator.start(interval: 10.0)

            // Capture sensor events to update UI
            simulator.eventCallback = { event in
                DispatchQueue.main.async {
                    viewModel.update(from: event)
                }
            }
        }
    }

    func colorForClassification(_ classification: String) -> Color {
        switch classification.lowercased() {
        case "danger":
            return .red
        case "warning":
            return .orange
        case "safe":
            return .green
        default:
            return .gray
        }
    }
    
    func getCSVFilePath() -> URL? {
        // Directly get URL from bundle, no copying needed if you just want to read
        return Bundle.main.url(forResource: "dataset_1", withExtension: "csv")
    }
}
