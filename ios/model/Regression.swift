//
//  Regression.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import CoreML

class Regression {
    
    private var model: co_lstm_regression_v1?

    init() {
        loadModel()
        //loadModel(with: .cpuOnly)
        //-- iOS 16 loadModel(with: .cpuAndNeuralEngine)
        //loadModel(with: .cpuAndGPU)
        //loadModel(with: .all)  // default
    }
    
    private func loadModel(with computeUnits: MLComputeUnits = .all) {
        do {
            let config = MLModelConfiguration()
            config.computeUnits = computeUnits
            
            model = try co_lstm_regression_v1(configuration: config)
            print("ML Regression Model loaded with compute units: \(computeUnits)")
        } catch {
            print("Failed to load Core ML regression model: \(error)")
            model = nil
        }
    }

    // Load the Core ML model
    private func loadModel() {
        do {
            model = try co_lstm_regression_v1(configuration: MLModelConfiguration())
            print("Machine Learning (ML) Regression Model: Loaded")
        } catch {
            print("Failed to load Core ML regression model: \(error)")
            model = nil
        }
    }

    // Predict CO ppm value from input sequence [30][18]
    func predict(sensorSequence: [[Double]]) -> Double? {
        guard let model = model else {
            print("Model not loaded.")
            return nil
        }

        // Check input dimensions: 30 time steps, 18 features
        guard sensorSequence.count == 30, sensorSequence[0].count == 18 else {
            print("Input must be a 30x18 matrix (timeSteps x features).")
            return nil
        }

        // Create MLMultiArray with shape [1, 30, 18] (as expected by Core ML LSTM)
        guard let inputArray = try? MLMultiArray(shape: [1, 30, 18], dataType: .double) else {
            print("Failed to create MLMultiArray.")
            return nil
        }

        // Fill the array
        for t in 0..<30 {
            for f in 0..<18 {
                let index: [NSNumber] = [0, NSNumber(value: t), NSNumber(value: f)]
                inputArray[index] = NSNumber(value: sensorSequence[t][f])
            }
        }

        // Run prediction
        guard let output = try? model.prediction(input_1: inputArray) else {
            print("Prediction failed.")
            return nil
        }
        
        return output.Identity[0].doubleValue
    }

    // Optional: Classify CO level based on thresholds
    func classify(sensorSequence: [[Double]]) -> String {
        guard let predictedPPM = predict(sensorSequence: sensorSequence) else {
            return "error"
        }

        // Thresholds can be adjusted based on health guidelines or application needs
        switch predictedPPM {
        case ..<9.0:
            return "safe"
        case 9.0..<35.0:
            return "warning"
        default:
            return "danger"
        }
    }
}
