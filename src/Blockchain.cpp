#include "Blockchain.hpp"
#include <iostream>

Blockchain::Blockchain(const std::string& dbPath) : storage(dbPath) {
    storage.open();
    auto storedBlocks = storage.loadBlocks();

    for (const auto& blockData : storedBlocks) {
        try {
            Block block = Block::fromJson(blockData.json);
            chain.push_back(std::move(block));
        } catch (const std::exception& e) {
            std::cerr << "Warning: failed to load block from storage: " << e.what() << std::endl;
            // Decide policy: skip corrupted blocks or throw.
            // Here we skip to allow chain recovery.
        }
    }

    if (chain.empty()) {
        Block genesis = createGenesisBlock();
        chain.push_back(genesis);
        storage.saveBlock({genesis.toJson()});
    }
}

Blockchain::~Blockchain() {
    storage.close();
}

Block Blockchain::createGenesisBlock() {
    return Block(0, std::chrono::system_clock::now(), {}, "0");
}

Block Blockchain::getLatestBlock() const {
    std::lock_guard<std::mutex> lock(chainMutex);
    if (chain.empty()) {
        throw std::runtime_error("Blockchain is empty");
    }
    return chain.back();
}

void Blockchain::addBlock(const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(chainMutex);

    Block last = getLatestBlock();
    Block newBlock(last.index + 1, std::chrono::system_clock::now(), data, last.hash);

    std::cout << "[DEBUG] New block created. Index: " << newBlock.index << std::endl;

    chain.push_back(newBlock);
    std::cout << "[DEBUG] Block added to in-memory chain." << std::endl;

    try {
        auto json = newBlock.toJson();
        std::cout << "[DEBUG] Block serialized to JSON." << std::endl;

        storage.saveBlock({json});
        std::cout << "[DEBUG] Block saved to storage." << std::endl;

    } catch (const std::exception& e) {
        chain.pop_back();
        std::cerr << "[ERROR] Failed to save block: " << e.what() << std::endl;
        throw;
    }
}

bool Blockchain::isValidChain() const {
    std::lock_guard<std::mutex> lock(chainMutex);

    for (size_t i = 1; i < chain.size(); ++i) {
        const Block& curr = chain[i];
        const Block& prev = chain[i - 1];

        std::string computedHash = Block::computeHash(curr.index, curr.timestamp, curr.data, curr.previousHash, curr.nonce);

        if (curr.hash != computedHash) {
            std::cerr << "Invalid hash at block " << curr.index << std::endl;
            return false;
        }
        if (curr.previousHash != prev.hash) {
            std::cerr << "Invalid previous hash link at block " << curr.index << std::endl;
            return false;
        }
    }
    return true;
}

void Blockchain::printChain() const {
    std::lock_guard<std::mutex> lock(chainMutex);
    for (const auto& block : chain) {
        std::cout << block.toJson() << std::endl;
    }
}