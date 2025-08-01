#include "SensorSimulator.hpp"
#include <iostream>
#include <chrono>

SensorSimulator::SensorSimulator(std::vector<SensorEvent> evts, Blockchain& bc, SmartContract& sc)
    : events(std::move(evts)), blockchain(bc), contract(sc) {}

SensorSimulator::~SensorSimulator() {
    stop();
}

void SensorSimulator::start(int intervalSeconds) {
    if (running.load()) return;  // Already running

    running = true;
    workerThread = std::thread(&SensorSimulator::run, this, intervalSeconds);
}

void SensorSimulator::stop() {
    if (!running.load()) return;

    running = false;
    if (workerThread.joinable())
        workerThread.join();
}

void SensorSimulator::run(int intervalSeconds) {
    while (running.load() && currentIndex < events.size()) {
        try {
            const SensorEvent& event = events[currentIndex++];
            auto data = event.toBlockData();

            blockchain.addBlock(data);    // Exceptions will propagate here
            contract.execute(data);

        } catch (const std::exception& e) {
            std::cerr << "Error processing sensor event: " << e.what() << std::endl;
            // Depending on policy, could stop or continue
        }

        for (int i = 0; i < intervalSeconds && running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}