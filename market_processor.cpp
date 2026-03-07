#include <atomic>
#include <memory>
#include <variant>
#include <ostream>
#include <thread>
#include <vector>
#include <iostream>
#include <array>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

#include "dynamic_thread_pool.hpp"
#include "lock_free_queue.hpp"
#include "async_task_manager.hpp"


// g++ --std=c++20 -O3 -pthread main3.cpp -o main3

// Market data types
struct MarketTick {
    std::string symbol;
    double price;
    int volume;
    std::chrono::steady_clock::time_point timestamp;
    
    MarketTick(const std::string& sym, double p, int v) 
        : symbol(sym), price(p), volume(v), timestamp(std::chrono::steady_clock::now()) {}

    MarketTick() = default;    
};

struct TradeSignal {
    std::string symbol;
    enum Action { BUY, SELL, HOLD } action;
    double confidence;
    std::string reason;
};

using MarketData = std::variant<MarketTick, TradeSignal>;

class RealTimeMarketProcessor {
private:
    DynamicThreadPool threadPool_;
    LockFreeQueue<MarketData> dataQueue_;
    AsyncTaskManager<TradeSignal> signalProcessor_;
    
    // Market data storage
    std::unordered_map<std::string, MarketTick> latestPrices_;
    mutable std::shared_mutex pricesMutex_;
    
    // Analytics components
    std::atomic<size_t> ticksProcessed_{0};
    std::atomic<size_t> signalsGenerated_{0};
    std::atomic<double> averageProcessingLatency_{0.0};
    
    // Configuration
    const double PRICE_CHANGE_THRESHOLD = 0.05; // 5% price change threshold
    const size_t BATCH_SIZE = 100;
    
public:
    RealTimeMarketProcessor(size_t minThreads = 4, size_t maxThreads = 16) 
        : threadPool_(minThreads, maxThreads) {
        
        // Start data processing pipeline
        startDataProcessor();
        startSignalGenerator();
        
        std::cout << "Real-time market processor initialized" << std::endl;
    }
    
    void ingestMarketData(const MarketTick& tick) {
        dataQueue_.push(MarketData{tick});
    }
    
    void ingestSignal(const TradeSignal& signal) {
        dataQueue_.push(MarketData{signal});
    }
    
private:
    void startDataProcessor() {
        // High-priority data ingestion processor
        threadPool_.submit([this]() {
            processDataStream();
        }, TaskPriority::CRITICAL, "data-processor");
    }
    
    void startSignalGenerator() {
        // Medium-priority signal generation
        threadPool_.submit([this]() {
            generateTradingSignals();
        }, TaskPriority::HIGH, "signal-generator");
    }
    
    void processDataStream() {
        std::vector<MarketData> batch;
        batch.reserve(BATCH_SIZE);
        
        while (true) {
            // Collect batch of data
            MarketData data;
            while (batch.size() < BATCH_SIZE && dataQueue_.pop(data)) {
                batch.push_back(std::move(data));
            }
            
            if (!batch.empty()) {
                processBatch(batch);
                batch.clear();
            } else {
                // No data available, yield to other threads
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
    
    void processBatch(const std::vector<MarketData>& batch) {
        auto startTime = std::chrono::steady_clock::now();
        
        for (const auto& data : batch) {
            std::visit([this](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                
                if constexpr (std::is_same_v<T, MarketTick>) {
                    processMarketTick(item);
                } else if constexpr (std::is_same_v<T, TradeSignal>) {
                    processTradeSignal(item);
                }
            }, data);
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration<double, std::micro>(endTime - startTime).count();
        updateLatencyMetrics(latency);
        
        ticksProcessed_.fetch_add(batch.size());
    }
    
    void processMarketTick(const MarketTick& tick) {
        MarketTick previousTick;
        bool hasHistory = false;
        
        {
            std::shared_lock<std::shared_mutex> readLock(pricesMutex_);
            auto it = latestPrices_.find(tick.symbol);
            if (it != latestPrices_.end()) {
                previousTick = it->second;
                hasHistory = true;
            }
        }
        
        // Update latest price (writer lock)
        {
            std::unique_lock<std::shared_mutex> writeLock(pricesMutex_);
            latestPrices_[tick.symbol] = tick;
        }
        
        // Check for significant price movements
        if (hasHistory) {
            double priceChange = std::abs(tick.price - previousTick.price) / previousTick.price;
            
            if (priceChange > PRICE_CHANGE_THRESHOLD) {
                // Submit async analysis task
                signalProcessor_.submitTask([this, tick, priceChange]() {
                    return analyzeSignificantMove(tick, priceChange);
                });
            }
        }
    }
    
    void processTradeSignal(const TradeSignal& signal) {
        // Process incoming trading signals
        threadPool_.submit([this, signal]() {
            executeTradeSignal(signal);
        }, TaskPriority::HIGH, "execute-signal-" + signal.symbol);
        
        signalsGenerated_.fetch_add(1);
    }
    
    TradeSignal analyzeSignificantMove(const MarketTick& tick, double priceChange) {
        // Simulate complex market analysis
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        TradeSignal::Action action = TradeSignal::HOLD;
        double confidence = 0.0;
        std::string reason = "Significant price movement detected";
        
        // Simple momentum-based strategy
        if (priceChange > PRICE_CHANGE_THRESHOLD) {
            action = TradeSignal::BUY;
            confidence = std::min(0.95, priceChange * 10);
            reason = "Strong upward momentum";
        }
        
        return TradeSignal{tick.symbol, action, confidence, reason};
    }
    
    void executeTradeSignal(const TradeSignal& signal) {
        // Simulate trade execution latency
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "TRADE EXECUTED: " << signal.symbol 
                  << " " << (signal.action == TradeSignal::BUY ? "BUY" : "SELL")
                  << " (confidence: " << signal.confidence << ")" << std::endl;
    }
    
    void generateTradingSignals() {
        while (true) {
            // Periodic signal generation based on market conditions
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            std::shared_lock<std::shared_mutex> readLock(pricesMutex_);
            
            for (const auto& [symbol, tick] : latestPrices_) {
                // Simple pattern-based signal generation
                if (shouldGenerateSignal(tick)) {
                    auto signal = generatePatternSignal(tick);
                    ingestSignal(signal);
                }
            }
        }
    }
    
    bool shouldGenerateSignal(const MarketTick& tick) const {
        // Simple time-based signal generation
        auto now = std::chrono::steady_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - tick.timestamp);
        
        return age.count() < 5; // Only generate signals for recent data
    }
    
    TradeSignal generatePatternSignal(const MarketTick& tick) {
        // Simulate pattern recognition
        return TradeSignal{tick.symbol, TradeSignal::HOLD, 0.3, "Pattern analysis"};
    }
    
    void updateLatencyMetrics(double latency) {
        double currentAvg = averageProcessingLatency_.load();
        double newAvg = (currentAvg * 0.95) + (latency * 0.05);
        averageProcessingLatency_.store(newAvg);
    }
    
public:
    struct SystemMetrics {
        size_t ticksProcessed;
        size_t signalsGenerated;
        double averageLatency;
        size_t queueSize;
        DynamicThreadPool::PoolStats threadPoolStats;
        size_t symbolsTracked;
    };
    
    SystemMetrics getMetrics() const {
        std::shared_lock<std::shared_mutex> readLock(pricesMutex_);
        
        return SystemMetrics{
            ticksProcessed_.load(),
            signalsGenerated_.load(),
            averageProcessingLatency_.load(),
            dataQueue_.size(),
            threadPool_.getStats(),
            latestPrices_.size()
        };
    }
    
    void printMetrics() const {
        auto metrics = getMetrics();
        
        std::cout << "\n=== Market Processor Metrics ===" << std::endl;
        std::cout << "Ticks processed: " << metrics.ticksProcessed << std::endl;
        std::cout << "Signals generated: " << metrics.signalsGenerated << std::endl;
        std::cout << "Average latency: " << metrics.averageLatency << " μs" << std::endl;
        std::cout << "Queue size: " << metrics.queueSize << std::endl;
        std::cout << "Symbols tracked: " << metrics.symbolsTracked << std::endl;
        
        threadPool_.printStats();
    }
};

int main()
{
    RealTimeMarketProcessor processor;

     // Market data simulation
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "TSLA", "AMZN"};
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Generate market data
    std::thread dataGenerator([&]() {
        std::uniform_real_distribution<> priceDist(100.0, 200.0);
        std::uniform_int_distribution<> volumeDist(100, 10000);
        std::uniform_int_distribution<> symbolDist(0, symbols.size() - 1);
        
        for (int i = 0; i < 10000; ++i) {
            std::string symbol = symbols[symbolDist(gen)];
            double price = priceDist(gen);
            int volume = volumeDist(gen);
            
            processor.ingestMarketData(MarketTick(symbol, price, volume));
            
            // Variable rate data generation
            std::this_thread::sleep_for(std::chrono::microseconds(100 + (i % 1000)));
        }
    });
    
    // Performance monitoring
    std::thread monitor([&processor]() {
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            processor.printMetrics();
        }
    });
    
    dataGenerator.join();
    monitor.join();
    
    std::cout << "Market processing simulation completed" << std::endl;
    
    return 0;
}