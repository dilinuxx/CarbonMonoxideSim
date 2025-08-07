//
//  Peer.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

class Peer {
    let id: String
    var lastSeen: Date = Date()

    init(id: String) {
        self.id = id
    }

    func send(_ data: Data) {
        // Placeholder for sending data via MultipeerConnectivity or similar
    }

    func receive(_ data: Data) {
        // Placeholder for receiving data
    }
}
