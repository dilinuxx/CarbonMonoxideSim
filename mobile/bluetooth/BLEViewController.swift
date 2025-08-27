//
//  BLEViewController.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/07/2025.
//

import UIKit

class BLEViewController: UIViewController, BLEPeripheralManagerDelegate {
    var blePeripheralManager: BLEPeripheralManager?
    
    let textView = UITextView()
    let inputField = UITextField()
    let sendButton = UIButton(type: .system)

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        print("BLEViewController loaded")
        
        blePeripheralManager = BLEPeripheralManager()
        blePeripheralManager?.delegate = self
        
        setupUI()
    }
    
    private func setupUI() {
        // Configure text view
        textView.translatesAutoresizingMaskIntoConstraints = false
        textView.isEditable = false
        textView.font = .systemFont(ofSize: 16)
        textView.text = "BLE Peripheral Initialized...\n"
        view.addSubview(textView)
        
        // Configure input field
        inputField.translatesAutoresizingMaskIntoConstraints = false
        inputField.placeholder = "Type your message..."
        inputField.borderStyle = .roundedRect
        view.addSubview(inputField)
        
        // Configure send button
        sendButton.translatesAutoresizingMaskIntoConstraints = false
        sendButton.setTitle("Send", for: .normal)
        sendButton.addTarget(self, action: #selector(sendButtonTapped), for: .touchUpInside)
        view.addSubview(sendButton)
        
        // Layout constraints
        NSLayoutConstraint.activate([
            textView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 16),
            textView.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            textView.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            
            inputField.topAnchor.constraint(equalTo: textView.bottomAnchor, constant: 16),
            inputField.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            inputField.trailingAnchor.constraint(equalTo: sendButton.leadingAnchor, constant: -8),
            inputField.heightAnchor.constraint(equalToConstant: 44),
            
            sendButton.centerYAnchor.constraint(equalTo: inputField.centerYAnchor),
            sendButton.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            sendButton.widthAnchor.constraint(equalToConstant: 60),
            
            inputField.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),
            textView.bottomAnchor.constraint(equalTo: inputField.topAnchor, constant: -16)
        ])
    }
    
    /*
    @objc private func sendButtonTapped() {
        guard let message = inputField.text, !message.isEmpty else { return }
        appendToLog("▶️ Sending: \(message)")
        blePeripheralManager?.sendMessage(message)
        inputField.text = ""
        inputField.resignFirstResponder()
    }
    */
    
    @objc private func sendButtonTapped() {
        // Example sensor values (replace with your real sensor readings)
        let timeS: Float = 0.0
        let coPpm: Float = 0.0
        let humidity: Float = 50.25
        let temperature: Float = 26.54
        let flowRate: Float = 243.05
        let heaterVoltage: Float = 0.88

        // Get current timestamp (milliseconds since 1970) as UInt32
        let timestampMS = UInt64(Date().timeIntervalSince1970 * 1000)


        // Create a Data buffer of 28 bytes: 4 bytes UInt32 + 6 * 4 bytes Float32
        var payload = Data()

        // Append timestamp in big-endian
        payload.append(contentsOf: withUnsafeBytes(of: timestampMS.bigEndian, Array.init))

        // Helper function to append Float32 in big-endian
        func appendFloatBE(_ value: Float) {
            var val = value.bitPattern.bigEndian
            payload.append(Data(bytes: &val, count: MemoryLayout<UInt32>.size))
        }

        // Append sensor values
        appendFloatBE(timeS)
        appendFloatBE(coPpm)
        appendFloatBE(humidity)
        appendFloatBE(temperature)
        appendFloatBE(flowRate)
        appendFloatBE(heaterVoltage)

        appendToLog("Sending binary payload with timestamp \(timestampMS)")

        // Send the binary payload via your BLE peripheral manager
        blePeripheralManager?.sendMessage(payload)

        inputField.text = ""
        inputField.resignFirstResponder()
    }
    
    // BLEPeripheralManagerDelegate
    func didReceiveMessage(_ message: String) {
        DispatchQueue.main.async {
            self.appendToLog("Received: \(message)")
        }
    }
    
    func didSendNotification(_ response: String) {
        DispatchQueue.main.async {
            self.appendToLog("Notified central: \(response)")
        }
    }
    
    private func appendToLog(_ text: String) {
        textView.text.append("\n" + text)
        let bottom = NSRange(location: textView.text.count - 1, length: 1)
        textView.scrollRangeToVisible(bottom)
    }
}
