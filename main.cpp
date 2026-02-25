#include <queue>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>
#include <iostream>
#include <chrono>

enum class TaskPriority {
    LOW = 1,
    NORMAL = 2,
    HIGH = 3,
    CRITICAL = 4
};

struct Task {
    std::function<void()> function;
    TaskPriority priority;
    std::chrono::steady_clock::time_point submitTime;
    std::string taskId;
    
    Task(std::function<void()> func, TaskPriority prio, const std::string& id = "")
        : function(std::move(func)), priority(prio), 
          submitTime(std::chrono::steady_clock::now()), taskId(id) {}

    Task() {}
};

struct TaskComparator {
    bool operator()(const Task& a, const Task& b) const {
        if (a.priority != b.priority) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        }
        return a.submitTime > b.submitTime; // Earlier submission has higher priority
    }
};

class DynamicThreadPool {
private:
    std::vector<std::thread> workers_;
    std::priority_queue<Task, std::vector<Task>, TaskComparator> taskQueue_;
    
    mutable std::mutex queueMutex_;
    mutable std::mutex workerMutex_;
    std::condition_variable condition_;
    
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> activeThreads_{0};
    std::atomic<size_t> totalTasksProcessed_{0};
    
    // Dynamic scaling parameters
    std::atomic<size_t> minThreads_;
    std::atomic<size_t> maxThreads_;
    std::atomic<size_t> currentThreads_{0};
    
    // Performance monitoring
    std::atomic<double> averageTaskTime_{0.0};
    std::atomic<size_t> queueHighWaterMark_{0};
    
    void workerThread() {
        while (true) {
            Task task;
            bool hasTask{false};
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                
                condition_.wait(lock, [this] {
                    return !taskQueue_.empty() || shutdown_.load();
                });
                
                if (shutdown_.load() && taskQueue_.empty())
                    break;
                
                if (!taskQueue_.empty()) {
                    task = taskQueue_.top();
                    taskQueue_.pop();
                    hasTask = true;
                }
            }

            if(hasTask)
                proccessTask(task);
        }
        
        currentThreads_.fetch_sub(1);
    }

    void proccessTask(const Task& task)
    {
        activeThreads_.fetch_add(1);
                
        auto startTime = std::chrono::steady_clock::now();
                
        try {
            task.function();
        } catch (const std::exception& e) {
            std::cout << "Task " << task.taskId << " failed: " << e.what() << std::endl;
        } catch (...) {
            std::cout << "Task " << task.taskId << " failed with unknown exception" << std::endl;
        }
                
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(endTime - startTime);
                
        updatePerformanceMetrics(duration.count());
                
        totalTasksProcessed_.fetch_add(1);
        activeThreads_.fetch_sub(1);
    }
    
    void updatePerformanceMetrics(double taskDuration) {
        // Simple exponential moving average
        double currentAvg = averageTaskTime_.load();
        double newAvg;
        do{
            newAvg = (currentAvg * 0.9) + (taskDuration * 0.1);
        }while(!averageTaskTime_.compare_exchange_weak(currentAvg, newAvg));
    }
    
    void scaleThreadPool() {
        size_t queueSize = getQueueSize();
        size_t current = currentThreads_.load();
        size_t active = activeThreads_.load();
        
        // Update high water mark
        size_t currentHighWater = queueHighWaterMark_.load();
        if (queueSize > currentHighWater) {
            queueHighWaterMark_.store(queueSize);
        }
        
        // Scale up if queue is growing and we have capacity
        if (queueSize > current * 2 && current < maxThreads_.load()) {
            addWorkerThread();
        }
        
        // Scale down if threads are mostly idle (simplified logic)
        if (queueSize == 0 && active < current / 2 && current > minThreads_.load()) {
            // In a real implementation, we'd implement controlled thread termination
            // For simplicity, we'll just track that we could scale down
        }
    }
    
    void addWorkerThread() {
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workers_.emplace_back(&DynamicThreadPool::workerThread, this);
        }
        currentThreads_.fetch_add(1);
        std::cout << "Scaled up to " << currentThreads_.load() << " threads" << std::endl;
    }

    void shutdown() {
        shutdown_.store(true);
        condition_.notify_all();
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        workers_.clear();
        std::cout << "Thread pool shutdown completed" << std::endl;
    }
    
public:
    DynamicThreadPool(size_t minThreads = 2, size_t maxThreads = std::thread::hardware_concurrency() - 4) 
        : minThreads_(minThreads), maxThreads_(maxThreads) {
        
        // Start with minimum threads
        for (size_t i = 0; i < minThreads; ++i) {
            addWorkerThread();
        }
        
        std::cout << "Dynamic thread pool initialized with " << minThreads << " threads (max: " << maxThreads << ")" << std::endl;
    }
    
    ~DynamicThreadPool() {
        shutdown();
    }
    
    template<typename Func>
    void submit(Func&& func, TaskPriority priority = TaskPriority::NORMAL, const std::string& taskId = "") {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            taskQueue_.emplace(std::forward<Func>(func), priority, taskId);
        }
        
        condition_.notify_one();
        
        // Trigger scaling evaluation
        scaleThreadPool();
    }
    
    size_t getQueueSize() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return taskQueue_.size();
    }
    
    struct PoolStats {
        size_t currentThreads;
        size_t activeThreads;
        size_t queueSize;
        size_t totalTasksProcessed;
        double averageTaskTime;
        size_t queueHighWaterMark;
    };
    
    PoolStats getStats() const {
        return PoolStats{
            currentThreads_.load(),
            activeThreads_.load(),
            getQueueSize(),
            totalTasksProcessed_.load(),
            averageTaskTime_.load(),
            queueHighWaterMark_.load()
        };
    }
    
    void printStats() const {
        auto stats = getStats();
        std::cout << "\n=== Thread Pool Statistics ===" << std::endl;
        std::cout << "Current threads: " << stats.currentThreads << std::endl;
        std::cout << "Active threads: " << stats.activeThreads << std::endl;
        std::cout << "Queue size: " << stats.queueSize << std::endl;
        std::cout << "Total tasks processed: " << stats.totalTasksProcessed << std::endl;
        std::cout << "Average task time: " << stats.averageTaskTime << " ms" << std::endl;
        std::cout << "Queue high water mark: " << stats.queueHighWaterMark << std::endl;
    }
};

int main()
{
    DynamicThreadPool pool;
    std::vector<std::thread> producers;
    std::atomic<int> counter{0};

    const int numberProducers = 4;
    const int numberTasks = 5000;

    for(int i{0}; i < numberProducers; ++i)
    {
        producers.emplace_back([&pool, &counter]{
        for(int j{0}; j < numberTasks; ++j) {
            TaskPriority priority = (TaskPriority) (j % (int)TaskPriority::CRITICAL + 1);
            pool.submit([]{
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }, priority);
        }
        });
        
    }

    for(auto &producer : producers)
        producer.join();

    pool.~DynamicThreadPool();  
    pool.printStats();

    return 0;
}