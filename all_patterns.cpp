#include <iostream>
#include <vector>
#include <thread>
#include <future>

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>
#include <random>

// g++ --std=c++20 -O3 -pthread all_patterns.cpp -o all_patterns

using namespace std;

// Global atomic counter for lock-free operations
atomic<int> globalCounter{0};

// ============================================================================
// PART 1: Futures and Promises for Asynchronous Operations
// ============================================================================

// 1: Implement async calculation using promise/future
void asyncCalculation(promise<int> resultPromise, int inputValue) {
    int result = inputValue * inputValue + 100;
    this_thread::sleep_for(chrono::milliseconds(100));
    resultPromise.set_value(result);
}

// 2: Create async task that might throw exception
void riskyAsyncTask(promise<string> resultPromise, int riskFactor) {    
    try {
        if(riskFactor > 50)
            throw std::runtime_error("Risk too high!");
        else
            resultPromise.set_value(to_string(riskFactor));    
    } catch (...) {
        resultPromise.set_exception(std::current_exception());
    }
}

// ============================================================================
// PART 2: Basic Thread Pool Implementation
// ============================================================================

class SimpleThreadPool {
private:
    vector<thread> workers;
    int numThreads;
    atomic<bool> stopThreadPool{false};
    queue<function<void()>> taskQueue;
    mutex queueMutex;
    condition_variable queueCondition;

public:
    SimpleThreadPool(int threads) : numThreads(threads) {
        initializePool();
    }
    
    ~SimpleThreadPool() {
        shutdown();
    }
    
    // 3: Initialize the thread pool
    void initializePool() {
        workers.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back(&SimpleThreadPool::workerFunction, this);
        }
    }
    
    // 4: Implement worker function that processes tasks from taskQueue
    void workerFunction() {
        while (!stopThreadPool.load()) {
            function<void()> task;

            {
                unique_lock<mutex> lock(queueMutex);
                queueCondition.wait(lock, [this]{ return !taskQueue.empty() || stopThreadPool.load(); });
                if (stopThreadPool.load() && taskQueue.empty())
                break;
                task = taskQueue.front();
                taskQueue.pop();
            }

            task();
        }

        cout << "thread " << this_thread::get_id() << " is finished\n";
    }
    
    // 5: Add task to the thread pool
    void enqueue(function<void()> task) {
        {
            lock_guard<mutex> lock(queueMutex);
            taskQueue.push(move(task));
        }
        queueCondition.notify_one();
    }
    
    // 6: Shutdown the thread pool
    void shutdown() {
        stopThreadPool.store(true);
        queueCondition.notify_all();

        for(auto& thread : workers)
        {
            if(thread.joinable())
                thread.join();
        }
    }
};

// ============================================================================
// PART 3: Atomic Operations and Lock-Free Programming
// ============================================================================

// 7: Implement lock-free increment function
void atomicIncrement(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        ++globalCounter;
    }
}

// 8: Implement compare-and-swap operation
bool atomicCompareAndSwap(atomic<int>& target, int expected, int desired) {
    return target.compare_exchange_strong(expected, desired);  // Replace with actual implementation
}

// ============================================================================
// PART 4: Combining Patterns - Async Task Processing
// ============================================================================

// 9: Process multiple tasks asynchronously and collect results
vector<future<int>> processTasksBatch(SimpleThreadPool& pool, const vector<int>& inputData) {
    vector<future<int>> futures;
    futures.reserve(inputData.size());
    
    for (int value : inputData) {
        auto prom = std::make_shared<promise<int>>();
        futures.push_back(prom->get_future());
        pool.enqueue([prom, value](){
            prom->set_value(value);
        });
    }
    
    return futures;
}

// Test functions for validation
void testFuturesAndPromises() {
    cout << "\n=== Testing Futures and Promises ===" << endl;
    
    // Test basic promise/future
    promise<int> calcPromise;
    future<int> calcFuture = calcPromise.get_future();
    
    thread calcThread(asyncCalculation, move(calcPromise), 10);
    
    cout << "Waiting for async calculation..." << endl;
    int result = calcFuture.get();
    cout << "Calculation result: " << result << endl;
    
    calcThread.join();
    
    // Test exception handling
    promise<string> riskPromise;
    future<string> riskFuture = riskPromise.get_future();
    
    thread riskThread(riskyAsyncTask, move(riskPromise), 25);
    
    try {
        string riskResult = riskFuture.get();
        cout << "Risk task result: " << riskResult << endl;
    } catch (const exception& e) {
        cout << "Risk task failed: " << e.what() << endl;
    }
    
    riskThread.join();
}

void testThreadPool() {
    cout << "\n=== Testing Thread Pool ===" << endl;
    
    SimpleThreadPool pool(4);
    
    // Submit several tasks
    for (int i = 1; i <= 8; ++i) {
        pool.enqueue([i]() {
            cout << "Task " << i << " executing on thread " 
                 << this_thread::get_id() << endl;
            this_thread::sleep_for(chrono::milliseconds(200));
            cout << "Task " << i << " completed" << endl;
        });
    }
    
    // Let tasks complete
    this_thread::sleep_for(chrono::seconds(2));
    
    cout << "Thread pool test completed" << endl;
}

void testAtomicOperations() {
    cout << "\n=== Testing Atomic Operations ===" << endl;
    
    globalCounter.store(0);
    
    // Test atomic increment with multiple threads
    vector<thread> incrementThreads;
    for (int i = 0; i < 4; ++i) {
        incrementThreads.emplace_back(atomicIncrement, 1000);
    }
    
    for (auto& t : incrementThreads) {
        t.join();
    }
    
    cout << "Expected counter value: 4000" << endl;
    cout << "Actual counter value: " << globalCounter.load() << endl;
    cout << "Atomic operations " << (globalCounter.load() == 4000 ? "PASSED" : "FAILED") << endl;
    
    // Test compare and swap
    atomic<int> testValue{10};
    bool swapped = atomicCompareAndSwap(testValue, 10, 20);
    cout << "Compare and swap (10->20): " << (swapped ? "SUCCESS" : "FAILED") 
         << ", value is now: " << testValue.load() << endl;
    
    bool notSwapped = atomicCompareAndSwap(testValue, 10, 30);
    cout << "Compare and swap (10->30): " << (notSwapped ? "SUCCESS" : "FAILED") 
         << ", value is still: " << testValue.load() << endl;
}

void testCombinedPatterns() {
    cout << "\n=== Testing Combined Patterns ===" << endl;
    
    SimpleThreadPool pool(3);
    vector<int> inputData = {1, 2, 3, 4, 5, 6, 7, 8};
    
    cout << "Processing batch of " << inputData.size() << " tasks..." << endl;
    
    auto futures = processTasksBatch(pool, inputData);
    
    cout << "Collecting results..." << endl;
    for (size_t i = 0; i < futures.size(); ++i) {
        int result = futures[i].get();
        cout << "Task " << i + 1 << " result: " << result << endl;
    }
    
    cout << "Combined patterns test completed" << endl;
}

int main() {
    cout << "=== Advanced Concurrency Patterns Lab ===" << endl;
    cout << "Learning futures/promises, thread pools, and atomic operations" << endl;
    
    testFuturesAndPromises();
    testThreadPool();
    testAtomicOperations();
    testCombinedPatterns();
    
    cout << "\n=== Lab Complete! ===" << endl;
    cout << "You've successfully implemented advanced concurrency patterns!" << endl;
    
    return 0;
}