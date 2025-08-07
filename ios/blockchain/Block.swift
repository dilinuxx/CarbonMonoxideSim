//
//  Block.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation
import CryptoKit

class Block: Codable {
    let index: Int
    let timestamp: Date
    let data: [String: AnyCodable]
    let previousHash: String
    var hash: String
    let nonce: Int

    init(index: Int, timestamp: Date, data: [String: AnyCodable], previousHash: String, nonce: Int = 0) {
        self.index = index
        self.timestamp = timestamp
        self.data = data
        self.previousHash = previousHash
        self.nonce = nonce
        self.hash = Block.computeHash(index: index, timestamp: timestamp, data: data, previousHash: previousHash, nonce: nonce)
    }

    static func computeHash(index: Int, timestamp: Date, data: [String: AnyCodable], previousHash: String, nonce: Int) -> String {
        let input = "\(index)\(timestamp.timeIntervalSince1970)\(data)\(previousHash)\(nonce)"
        let hash = SHA256.hash(data: Data(input.utf8))
        return hash.compactMap { String(format: "%02x", $0) }.joined()
    }
}
