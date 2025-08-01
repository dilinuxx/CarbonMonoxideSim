#include "Block.hpp"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

Block::Block(int idx, std::chrono::system_clock::time_point ts,
             const nlohmann::json& data,
             const std::string& previousHash, int nonce)
    : index(idx), timestamp(ts), data(data), previousHash(previousHash), nonce(nonce) {
    hash = computeHash(index, timestamp, data, previousHash, nonce);
}

std::string Block::computeHash(int index, std::chrono::system_clock::time_point timestamp,
                               const nlohmann::json& data,
                               const std::string& previousHash, int nonce) {
    std::stringstream ss;
    ss << index
       << std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count()
       << data.dump()
       << previousHash
       << nonce;

    std::string input = ss.str();
    unsigned char hash_bytes[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash_bytes);

    std::ostringstream result;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        result << std::hex << std::setw(2) << std::setfill('0') << (int)hash_bytes[i];
    }
    return result.str();
}

std::string Block::toJson() const {
    return nlohmann::json{
        {"index", index},
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count()},
        {"data", data},
        {"previousHash", previousHash},
        {"hash", hash},
        {"nonce", nonce}
    }.dump();
}

Block Block::fromJson(const std::string& jsonStr) {
    try {
        auto json = nlohmann::json::parse(jsonStr);

        if (!json.contains("index") || !json.contains("timestamp") || !json.contains("data") ||
            !json.contains("previousHash") || !json.contains("hash") || !json.contains("nonce")) {
            throw std::runtime_error("Invalid Block JSON: missing required fields");
        }

        int idx = json.at("index").get<int>();
        auto ts = std::chrono::system_clock::time_point(std::chrono::seconds(json.at("timestamp").get<uint64_t>()));
        nlohmann::json data = json.at("data");
        std::string prevHash = json.at("previousHash").get<std::string>();
        int nonce = json.at("nonce").get<int>();

        Block block(idx, ts, data, prevHash, nonce);

        // Validate hash matches computed hash (optional, good for integrity check)
        if (block.hash != json.at("hash").get<std::string>()) {
            throw std::runtime_error("Block hash mismatch: data may be corrupted");
        }

        return block;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("JSON parse error in Block::fromJson: ") + e.what());
    }
}