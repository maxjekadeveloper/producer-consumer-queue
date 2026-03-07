#include <atomic>

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