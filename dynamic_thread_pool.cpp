#include "dynamic_thread_pool.hpp"

int main()
{
    DynamicThreadPool pool;
    std::vector<std::thread> producers;

    const int numberProducers = 4;
    const int numberTasks = 5000;

    for(int i{0}; i < numberProducers; ++i)
    {
        producers.emplace_back([&pool]{
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