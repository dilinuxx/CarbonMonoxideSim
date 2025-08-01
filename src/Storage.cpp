#include "Storage.hpp"
#include <sqlite3.h>
#include <iostream>

namespace {
    // RAII wrapper for sqlite3_stmt*
    struct SQLiteStmt {
        sqlite3_stmt* stmt = nullptr;
        ~SQLiteStmt() { if (stmt) sqlite3_finalize(stmt); }
    };
}

Storage::Storage(const std::string& dbPath) : db_(nullptr), dbPath_(dbPath) {}

Storage::~Storage() {
    close();
}

void Storage::open() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (sqlite3_open(dbPath_.c_str(), reinterpret_cast<sqlite3**>(&db_)) != SQLITE_OK) {
        throw std::runtime_error("Failed to open database: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
    }

    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS blocks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            data TEXT NOT NULL
        );
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(reinterpret_cast<sqlite3*>(db_), createTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create table: ";
        if (errMsg) {
            msg += errMsg;
            sqlite3_free(errMsg);
        }
        throw std::runtime_error(msg);
    }
}

void Storage::close() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (db_) {
        sqlite3_close(reinterpret_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

void Storage::saveBlock(const BlockData& block) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!db_) throw std::runtime_error("Database not open");

    const char* insertSQL = "INSERT INTO blocks (data) VALUES (?);";
    SQLiteStmt stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), insertSQL, -1, &stmt.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
    }

    if (sqlite3_bind_text(stmt.stmt, 1, block.json.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("Failed to bind parameter: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
    }

    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        throw std::runtime_error("Failed to execute insert: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
    }
}

std::vector<BlockData> Storage::loadBlocks() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    std::vector<BlockData> blocks;
    if (!db_) throw std::runtime_error("Database not open");

    const char* querySQL = "SELECT data FROM blocks ORDER BY id ASC;";
    SQLiteStmt stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), querySQL, -1, &stmt.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare select statement: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
    }

    while (true) {
        int rc = sqlite3_step(stmt.stmt);
        if (rc == SQLITE_ROW) {
            const unsigned char* data = sqlite3_column_text(stmt.stmt, 0);
            if (data) {
                blocks.push_back(BlockData{ std::string(reinterpret_cast<const char*>(data)) });
            }
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            throw std::runtime_error("Error while reading blocks: " + std::string(sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_))));
        }
    }

    return blocks;
}