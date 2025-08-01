#pragma once
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

/**
 * @brief Represents a block in the blockchain.
 * 
 * Stores index, timestamp, data (JSON), previous block's hash, hash of this block,
 * and nonce (for extensibility, e.g., proof-of-work).
 */
class Block {
public:
    int index;
    std::chrono::system_clock::time_point timestamp;
    nlohmann::json data;
    std::string previousHash;
    std::string hash;
    int nonce;

    /**
     * @brief Construct a new Block object
     * 
     * @param idx Block height/index
     * @param ts Timestamp of block creation
     * @param data JSON data payload
     * @param previousHash Hash of previous block in chain
     * @param nonce Optional nonce for extensibility
     */
    Block(int idx, std::chrono::system_clock::time_point ts,
          const nlohmann::json& data,
          const std::string& previousHash, int nonce = 0);

    /**
     * @brief Compute SHA-256 hash of block contents.
     * 
     * @param index Block index
     * @param timestamp Block timestamp
     * @param data JSON data payload
     * @param previousHash Previous block hash
     * @param nonce Nonce value
     * @return std::string Hex-encoded SHA-256 hash string
     */
    static std::string computeHash(int index, std::chrono::system_clock::time_point timestamp,
                                   const nlohmann::json& data,
                                   const std::string& previousHash, int nonce);

    /**
     * @brief Serialize block to JSON string
     * 
     * @return std::string JSON representation of block
     */
    std::string toJson() const;

    /**
     * @brief Deserialize block from JSON string
     * 
     * Throws if JSON is invalid or missing required fields.
     * 
     * @param jsonStr JSON string representing a Block
     * @return Block Deserialized Block object
     */
    static Block fromJson(const std::string& jsonStr);
};
