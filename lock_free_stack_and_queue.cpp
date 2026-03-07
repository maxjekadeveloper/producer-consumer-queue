
#include <memory>
#include <type_traits>
#include <ostream>
#include <thread>
#include <vector>
#include <iostream>
#include <array>
#include <chrono>

#include "lock_free_queue.hpp"
#include "lock_free_stack.hpp"



// Performance benchmarking utility
class LockFreeBenchmark {
public:
    template<typename Container>
    static void benchmarkContainer(const std::string& containerName, 
                                 int operations, int producerThreads, int consumerThreads) {
        Container container;
        std::atomic<bool> start{false};
        std::atomic<int> itemsProduced{0};
        std::atomic<int> itemsConsumed{0};
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < producerThreads; ++i) {
            producers.emplace_back([&, i]() {
                while (!start.load()) { /* spin wait */ }
                
                int itemsPerProducer = operations / producerThreads;
                for (int j = 0; j < itemsPerProducer; ++j) {
                    container.push(i * 1000 + j);
                    itemsProduced.fetch_add(1);
                }
            });
        }
        
        // Consumer threads
        std::vector<std::thread> consumers;
        for (int i = 0; i < consumerThreads; ++i) {
            consumers.emplace_back([&]() {
                while (!start.load()) { /* spin wait */ }
                
                int item;
                while (itemsConsumed.load() < operations) {
                    if (container.pop(item)) {
                        itemsConsumed.fetch_add(1);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        // Start benchmark
        start.store(true);
        
        // Wait for completion
        for (auto& t : producers) t.join();
        for (auto& t : consumers) t.join();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        std::cout << containerName << " Benchmark Results:" << std::endl;
        std::cout << "  Operations: " << operations << std::endl;
        std::cout << "  Producer threads: " << producerThreads << std::endl;
        std::cout << "  Consumer threads: " << consumerThreads << std::endl;
        std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "  Items produced: " << itemsProduced.load() << std::endl;
        std::cout << "  Items consumed: " << itemsConsumed.load() << std::endl;
        std::cout << "  Throughput: " << (operations * 1000.0 / duration.count()) << " ops/sec" << std::endl;
        std::cout << std::endl;
    }
};

int main()
{
    auto threads = std::thread::hardware_concurrency() / 4;
    std::cout << "Starting program...\n";
    std::cout << "hardware_concurrency = " << std::thread::hardware_concurrency() << '\n';
    //LockFreeBenchmark::benchmarkContainer<LockFreeQueue<int>>("LockFreeQueue", 100000, 2, 2);
    LockFreeBenchmark::benchmarkContainer<LockFreeStack<int>>("LockFreeStack", 100000, 2, 2);
    std::cout << "Ending program\n";
    return 0;
}