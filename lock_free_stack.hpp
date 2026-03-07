#include <atomic>

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