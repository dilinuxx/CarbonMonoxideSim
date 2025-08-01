#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <stdexcept>

struct BlockData {
    std::string json;
};

class Storage {
public:
    explicit Storage(const std::string& dbPath);
    ~Storage();

    void open();                 // Throws std::runtime_error on failure
    void close();

    void saveBlock(const BlockData& block);    // Throws on failure
    std::vector<BlockData> loadBlocks();       // Throws on failure

private:
    void* db_;
    std::string dbPath_;
    std::mutex dbMutex_;        // Protects SQLite access
};

