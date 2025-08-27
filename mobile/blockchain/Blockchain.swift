//
//  Blockchain.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

class Blockchain {
    private(set) var chain: [Block] = []

    init() {
        chain.append(createGenesisBlock())
    }

    func createGenesisBlock() -> Block {
        return Block(index: 0, timestamp: Date(), data: [:], previousHash: "0")
    }

    func getLatestBlock() -> Block {
        return chain.last!
    }

    func addBlock(data: [String: AnyCodable]) {
        let lastBlock = getLatestBlock()
        let newBlock = Block(index: lastBlock.index + 1,
                             timestamp: Date(),
                             data: data,
                             previousHash: lastBlock.hash)
        chain.append(newBlock)
    }

    func isValidChain() -> Bool {
        for i in 1..<chain.count {
            let current = chain[i]
            let previous = chain[i - 1]
            if current.hash != Block.computeHash(index: current.index,
                                                 timestamp: current.timestamp,
                                                 data: current.data,
                                                 previousHash: current.previousHash,
                                                 nonce: current.nonce) {
                return false
            }
            if current.previousHash != previous.hash {
                return false
            }
        }
        return true
    }
}
