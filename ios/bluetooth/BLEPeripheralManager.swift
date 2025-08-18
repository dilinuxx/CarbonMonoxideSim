//
//  BLEPeripheralManagerDelegate.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/07/2025.
//

import CoreBluetooth
import UIKit

// Delegate Protocol
protocol BLEPeripheralManagerDelegate: AnyObject {
    func didReceiveMessage(_ message: String)
    func didSendNotification(_ response: String)
}

// BLEPeripheralManager
class BLEPeripheralManager: NSObject, CBPeripheralManagerDelegate {
    private var peripheralManager: CBPeripheralManager!
    private var characteristic: CBMutableCharacteristic!
    weak var delegate: BLEPeripheralManagerDelegate?

    override init() {
        super.init()
        peripheralManager = CBPeripheralManager(delegate: self, queue: nil)
    }

    // State Update
    func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        if peripheral.state == .poweredOn {
            print("Bluetooth is powered on")

            // Characteristic with read, write, and notify properties
            characteristic = CBMutableCharacteristic(
                type: CBUUID(string: "87654321-4321-6789-4321-0fedcba98765"),
                properties: [.read, .write, .notify],
                value: nil,
                permissions: [.readable, .writeable]
            )

            let service = CBMutableService(type: CBUUID(string: "12345678-1234-5678-1234-56789abcdef0"), primary: true)
            service.characteristics = [characteristic]
            peripheralManager.add(service)

            // Start advertising
            peripheralManager.startAdvertising([
                CBAdvertisementDataServiceUUIDsKey: [service.uuid],
                CBAdvertisementDataLocalNameKey: "iPhoneBLEPeripheral"
            ])
            
            print("Advertising started")
        } else {
            print("Bluetooth not available: \(peripheral.state.rawValue)")
        }
    }
        
    func startAdvertising(with payload: Data) {
        guard peripheralManager.state == .poweredOn else {
            print("Bluetooth not powered on")
            return
        }
        
        // Stop previous advertising first
        peripheralManager.stopAdvertising()
        
        // Advertise the service UUID and local name
        let advertisementData: [String: Any] = [
            CBAdvertisementDataLocalNameKey: "iPhoneBLEPeripheral",
            CBAdvertisementDataServiceUUIDsKey: [CBUUID(string: "12345678-1234-5678-1234-56789abcdef0")]
        ]
        
        peripheralManager.startAdvertising(advertisementData)
        print("Started advertising service for chat with payload size: \(payload.count) bytes")
        
        // Optionally, you can send the payload as a notification or update characteristic value after connection,
        // since BLE advertising packets are limited in size and your Python code reads/writes via characteristic.
    }
    
    func stopAdvertising() {
        peripheralManager?.stopAdvertising()
        print("Advertising stopped")
    }

    // Read Request
    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        print("Read request received")
        let response = "Hello from iPhone"
        request.value = response.data(using: .utf8)
        peripheralManager.respond(to: request, withResult: .success)
    }

    // Write Request
    func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        for request in requests {
            if let value = request.value,
               let message = String(data: value, encoding: .utf8) {
                print("Received from central:", message)
                delegate?.didReceiveMessage(message)

                // Build response message
                let responseMessage = "Received your message: \(message)"
                let responseData = responseMessage.data(using: .utf8)

                // Update value and notify subscribed centrals
                characteristic.value = responseData
                if let data = responseData {
                    let success = peripheralManager.updateValue(data, for: characteristic, onSubscribedCentrals: nil)
                    if success {
                        print("Notification sent to central")
                        delegate?.didSendNotification(responseMessage)
                    } else {
                        print("Failed to send notification")
                    }
                }
            }
        }

        // Respond to the first request
        if let firstRequest = requests.first {
            peripheralManager.respond(to: firstRequest, withResult: .success)
        }
    }

    // Subscriptions
    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didSubscribeTo characteristic: CBCharacteristic) {
        print("Central subscribed to characteristic")
    }

    func peripheralManager(_ peripheral: CBPeripheralManager, central: CBCentral, didUnsubscribeFrom characteristic: CBCharacteristic) {
        print("Central unsubscribed from characteristic")
    }
}

extension BLEPeripheralManager {
    // Send text to connected central
    func sendMessage(_ message: String) {
        guard let data = message.data(using: .utf8) else { return }
        characteristic.value = data
        let success = peripheralManager.updateValue(data,
                                                    for: characteristic,
                                                    onSubscribedCentrals: nil)
        if success {
            delegate?.didSendNotification(message)
        } else {
            print("Failed to send message to central")
        }
    }
    
    func sendMessage(_ message: Data) {
        characteristic.value = message
        let success = peripheralManager.updateValue(message,
                                                    for: characteristic,
                                                    onSubscribedCentrals: nil)
        if success {
            delegate?.didSendNotification("Sent binary payload (\(message.count) bytes)")
        } else {
            print("Failed to send binary message to central")
        }
    }
}

