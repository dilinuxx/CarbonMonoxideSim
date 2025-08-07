//
//  CryptoManager.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation
import CryptoKit

class CryptoManager {
    static func hash(_ input: String) -> String {
        let hashed = SHA256.hash(data: input.data(using: .utf8)!)
        return hashed.compactMap { String(format: "%02x", $0) }.joined()
    }

    static func sign(data: Data, privateKey: P256.Signing.PrivateKey) -> Data {
        let signature = try! privateKey.signature(for: data)
        return signature.derRepresentation
    }

    static func verify(data: Data, signature: Data, publicKey: P256.Signing.PublicKey) -> Bool {
        do {
            let sig = try P256.Signing.ECDSASignature(derRepresentation: signature)
            return publicKey.isValidSignature(sig, for: data)
        } catch {
            return false
        }
    }
}
