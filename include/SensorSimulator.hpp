#pragma once
#include "Blockchain.hpp"
#include "SmartContract.hpp"
#include "SensorEvent.hpp"
#include <vector>
#include <atomic>
#include <thread>

class SensorSimulator {
public:
    SensorSimulator(std::vector<SensorEvent> evts, Blockchain& bc, SmartContract& sc);
    ~SensorSimulator();

    // Starts simulation in background thread, interval in seconds configurable
    void start(int intervalSeconds = 5);

    // Gracefully stops the simulator thread
    void stop();

private:
    std::vector<SensorEvent> events;
    size_t currentIndex = 0;
    Blockchain& blockchain;
    SmartContract& contract;

    std::atomic<bool> running{false};
    std::thread workerThread;

    void run(int intervalSeconds);
};