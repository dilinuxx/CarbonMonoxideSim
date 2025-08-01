#pragma once
#include "Block.hpp"
#include "Storage.hpp"
#include <vector>
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

/**
 * @brief Lightweight blockchain class managing the chain of blocks.
 * 
 * Loads and saves blocks from Storage. Not thread-safe internally, 
 * so synchronization must be handled externally if accessed concurrently.
 */
class Blockchain {
public:
    explicit Blockchain(const std::string& dbPath);
    ~Blockchain();

    /**
     * @brief Add a new block with the given JSON data.
     * 
     * @param data JSON data payload for the new block
     * @throws std::runtime_error on storage or validation failure
     */
    void addBlock(const nlohmann::json& data);

    /**
     * @brief Validate the blockchain's integrity.
     * 
     * @return true if valid, false if chain is corrupted
     */
    bool isValidChain() const;

    /**
     * @brief Print all blocks in the chain to stdout.
     */
    void printChain() const;

private:
    std::vector<Block> chain;
    Storage storage;
    mutable std::mutex chainMutex; // Guards access to chain

    /**
     * @brief Create the genesis (first) block.
     * 
     * @return Block Genesis block
     */
    Block createGenesisBlock();

    /**
     * @brief Get the latest block in the chain.
     * 
     * @return Block Latest block
     */
    Block getLatestBlock() const;
};