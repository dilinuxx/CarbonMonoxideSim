//
//  ConsensusEngine.swift
//  CarbonMonoxide Sim
//
//  Created by Emem Udoh, Inc on 18/06/2025.
//

import Foundation

protocol ConsensusEngine {
    func proposeBlock(_ block: Block)
    func receiveVote(from peer: Peer, for block: Block)
    func reachConsensus(for block: Block) -> Bool
}
