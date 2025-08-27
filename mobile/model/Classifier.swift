//
//  Classifier.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//


import CoreML

class Classifier {
    
    private var model: co_lstm_classifier_v1?

    init() {
        loadModel()
    }

    // Load the Core ML model
    private func loadModel() {
        do {
            model = try co_lstm_classifier_v1(configuration: MLModelConfiguration())
            print("Machine Learning (ML) Classifier Model: Loaded")
        } catch {
            print("Failed to load Core ML classifier model: \(error)")
            model = nil
        }
    }

    // Run prediction with sensor input
    func classify(sensorSequence: [[Double]]) -> String {
        guard let model = model else {
            let error = "model not loaded"
            print(error)
            return error
        }

        guard sensorSequence.count == 10 && sensorSequence[0].count == 4 else {
            let error = "input sequence invalid"
            print(error)
            return error
        }

        guard let inputArray = try? MLMultiArray(shape: [1, 10, 4], dataType: .double) else {
            let error = "failed to create input array"
            print(error)
            return error
        }
        
        for i in 0..<10 {
            for j in 0..<4 {
                let index: [NSNumber] = [0, NSNumber(value: i), NSNumber(value: j)]
                inputArray[index] = NSNumber(value: sensorSequence[i][j])
            }
        }
        
        guard let output = try? model.prediction(lstm_input: inputArray) else {
            let error = "prediction failed"
            print(error)
            return error
        }

        let probabilities = output.Identity
        var maxIndex = 0
        var maxValue = probabilities[0].floatValue
        for i in 1..<probabilities.count {
            let val = probabilities[i].floatValue
            if val > maxValue {
                maxValue = val
                maxIndex = i
            }
        }

        let labels = ["safe", "warning", "danger"]
        return labels[maxIndex]
    }
}
