#include <iostream>
#include <future>
#include <vector>
#include <chrono>
#include <random>
#include <functional>
#include <exception>
#include <memory>
#include <thread>

template<typename T>
class AsyncTaskManager {
private:
    // Get completion status of all tasks
    struct TaskStatus {
        size_t completed = 0;
        size_t pending = 0;
        size_t failed = 0;
    };

    std::vector<std::future<T>> activeTasks_;
    std::mutex tasksMutex_;
    TaskStatus status;
    
public:
    // Submit async task with custom executor
    template<typename Func, typename... Args>
    const auto & submitTask(Func&& func, Args&&... args) {
        auto task = std::make_shared<std::packaged_task<T()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        
        std::future<T> future = task->get_future();
        
        // Execute in separate thread
        std::thread([task]() {
            try {
                (*task)();
            } catch (...) {
                // Exception automatically captured by packaged_task
            }
        }).detach();
        
        {
            std::lock_guard<std::mutex> lock(tasksMutex_);
            activeTasks_.push_back(std::move(future));
        }
        
        return activeTasks_;
    }
    
    // Wait for all tasks with timeout
    std::vector<T> waitForAll(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::vector<T> results;
        
        std::lock_guard<std::mutex> lock(tasksMutex_);
        for (auto& future : activeTasks_) {
            try {
                if (future.wait_for(timeout) == std::future_status::ready) {
                    results.push_back(future.get());
                    status.completed++;
                } else {
                    std::cout << "Task timed out" << std::endl;
                    status.pending++;
                }
            } catch (const std::exception& e) {
                std::cout << "Task failed: " << e.what() << std::endl;
                status.failed++;
            }
        }
        
        activeTasks_.clear();
        return results;
    }
    
    const TaskStatus& getStatus() const {
        return status;
    }
};