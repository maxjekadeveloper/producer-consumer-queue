#include <atomic>
#include <memory>
#include <type_traits>
#include <ostream>
#include <thread>
#include <vector>
#include <iostream>
#include <array>


template<typename T>
class LockFreeQueue {
private:
    struct Node {
        std::atomic<T*> data{nullptr};
        std::atomic<Node*> next{nullptr};
        
        Node() = default;
        
        explicit Node(T item) {
            data.store(new T(std::move(item)), std::memory_order_relaxed);
        }
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    std::atomic<size_t> size_{0};
    
    // Memory reclamation using hazard pointers (simplified)
    static constexpr size_t MAX_HAZARD_POINTERS = 16;
    thread_local static std::array<std::atomic<Node*>, MAX_HAZARD_POINTERS> hazardPointers;
    static std::atomic<size_t> hazardPointerIndex;
    
    Node* acquireHazardPointer(Node* node) {
        size_t index = hazardPointerIndex.fetch_add(1, std::memory_order_relaxed) % MAX_HAZARD_POINTERS;
        hazardPointers[index].store(node, std::memory_order_release);
        return node;
    }
    
    void releaseHazardPointer(Node* node) {
        for (auto& hp : hazardPointers) {
            if (hp.load(std::memory_order_acquire) == node) {
                hp.store(nullptr, std::memory_order_release);
                break;
            }
        }
    }
    
    bool isHazardous(Node* node) {
        for (const auto& hp : hazardPointers) {
            if (hp.load(std::memory_order_acquire) == node) {
                return true;
            }
        }
        return false;
    }
    
public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy, std::memory_order_relaxed);
        tail_.store(dummy, std::memory_order_relaxed);
    }
    
    ~LockFreeQueue() {
        while (Node* node = head_.load(std::memory_order_relaxed)) {
            head_.store(node->next.load(std::memory_order_relaxed), std::memory_order_relaxed);
            delete node;
        }
    }
    
    void push(T item) {
        Node* newNode = new Node(std::move(item));
        
        while (true) {
            Node* last = tail_.load(std::memory_order_acquire);
            Node* next = last->next.load(std::memory_order_acquire);
            
            if (last == tail_.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    // Try to link new node at the end of the list
                    if (last->next.compare_exchange_weak(next, newNode, 
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed)) {
                        // Successfully added new node, try to swing tail
                        tail_.compare_exchange_weak(last, newNode,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed);
                        size_.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                } else {
                    // Tail is lagging behind, try to advance it
                    tail_.compare_exchange_weak(last, next,
                                               std::memory_order_release,
                                               std::memory_order_relaxed);
                }
            }
        }
    }
    
    bool pop(T& result) {
        while (true) {
            Node* first = head_.load(std::memory_order_acquire);
            Node* last = tail_.load(std::memory_order_acquire);
            Node* next = first->next.load(std::memory_order_acquire);
            
            if (first == head_.load(std::memory_order_acquire)) {
                if (first == last) {
                    if (next == nullptr) {
                        // Queue is empty
                        return false;
                    }
                    // Tail is lagging behind, advance it
                    tail_.compare_exchange_weak(last, next,
                                               std::memory_order_release,
                                               std::memory_order_relaxed);
                } else {
                    // Read data before potential dequeue
                    if (next == nullptr) {
                        continue;
                    }
                    
                    T* data = next->data.load(std::memory_order_acquire);
                    if (data == nullptr) {
                        continue;
                    }
                    
                    // Try to swing head to next node
                    if (head_.compare_exchange_weak(first, next,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
                        result = *data;
                        delete data;
                        size_.fetch_sub(1, std::memory_order_relaxed);
                        
                        // Safe to reclaim first node (simplified - in production use proper hazard pointers)
                        if (!isHazardous(first)) {
                            delete first;
                        }
                        
                        return true;
                    }
                }
            }
        }
    }
    
    bool empty() const {
        return size_.load(std::memory_order_acquire) == 0;
    }
    
    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }
};

// Thread-local storage initialization
template<typename T>
thread_local std::array<std::atomic<typename LockFreeQueue<T>::Node*>, LockFreeQueue<T>::MAX_HAZARD_POINTERS> 
    LockFreeQueue<T>::hazardPointers{};

template<typename T>
std::atomic<size_t> LockFreeQueue<T>::hazardPointerIndex{0};

// Lock-free stack for comparison
template<typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        Node* next;
        
        Node(T item) : data(std::move(item)), next(nullptr) {}
    };
    
    std::atomic<Node*> head_{nullptr};
    std::atomic<size_t> size_{0};
    
public:
    void push(T item) {
        Node* newNode = new Node(std::move(item));
        newNode->next = head_.load(std::memory_order_relaxed);
        
        while (!head_.compare_exchange_weak(newNode->next, newNode,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
            // Loop until successful
        }
        
        size_.fetch_add(1, std::memory_order_relaxed);
    }
    
    bool pop(T& result) {
        Node* head = head_.load(std::memory_order_acquire);
        
        while (head && !head_.compare_exchange_weak(head, head->next,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
            // Retry with updated head
        }
        
        if (!head) {
            return false;
        }
        
        result = std::move(head->data);
        size_.fetch_sub(1, std::memory_order_relaxed);
        
        // In production, use proper memory reclamation
        delete head;
        return true;
    }
    
    bool empty() const {
        return head_.load(std::memory_order_acquire) == nullptr;
    }
    
    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }
};

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
    LockFreeBenchmark::benchmarkContainer<LockFreeQueue<int>>("LockFreeQueue", 100000, threads, threads);
    LockFreeBenchmark::benchmarkContainer<LockFreeStack<int>>("LockFreeStack", 100000, threads, threads);
    std::cout << "Ending program\n";
    return 0;
}