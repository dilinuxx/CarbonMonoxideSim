//
//  BLECentralManager.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/07/2025.
//

import Foundation
import CoreBluetooth


class BLECentralManager: NSObject, CBCentralManagerDelegate {
    private var centralManager: CBCentralManager!
    private let serviceUUID = CBUUID(string: "12345678-1234-5678-1234-56789abcdef0")

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            centralManager.scanForPeripherals(withServices: [serviceUUID], options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
            print("Started scanning for CO alert devices")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard let serviceData = advertisementData[CBAdvertisementDataServiceDataKey] as? [CBUUID: Data],
              let payload = serviceData[serviceUUID] else { return }

        // Decode payload
        if let alert = decodePayload(payload) {
            // Check alert flag
            if alert.alertFlag == 1 {
                // Calculate delay
                let now = UInt32(Date().timeIntervalSince1970)
                let delay = now > alert.timestamp ? now - alert.timestamp : 0

                print("Received alert from device \(alert.deviceID), delay: \(delay)s, CO: \(Double(alert.coLevel)/100) ppm")
            }
        }
    }

    func decodePayload(_ data: Data) -> SensorAlertPayload? {
        return SensorAlertPayload.decode(data)
    }
}
