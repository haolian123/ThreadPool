#ifndef THREAD_POOL
#define THREAD_POOL
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool{
public:
    //构造函数，传入工作线程数量
    ThreadPool(size_t threadsNumber);

    //将函数添加到工作线程中
    template<class F,class... Args>
    // auto addTasksQueue(F&& f, Args&&... args)
    //     -> std::future<typename std::result_of<F(Args...)>::type>;
    std::future<typename std::result_of<F(Args...)>::type> addTasksQueue(F&& f, Args&&... args) ;
    //析构函数，释放所有线程
    ~ThreadPool();

private:
    //工作线程数组
    std::vector<std::thread>workers;

    //任务数组
    std::queue<std::function<void()>>tasksQueue;

    //互斥锁
    std::mutex tasksQueueMutex;

    //条件变量
    std::condition_variable conditionVariable;

    //工作函数
    void work();

    bool threadPoolStop=false;
    
};


//将函数添加到工作线程中
template<class F,class... Args>
std::future<typename std::result_of<F(Args...)>::type> ThreadPool::addTasksQueue(F&& f, Args&&... args)    
{
    using return_type=typename std::result_of<F(Args...)>::type;

    auto task=std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f),std::forward<Args>(args)...)
    );


    auto res=task->get_future();
    {
        std::unique_lock<std::mutex> lock(tasksQueueMutex);
        // conditionVariable.wait(lock);
        if(threadPoolStop){
            throw std::runtime_error("线程池已停止！");
        }
        tasksQueue.emplace([task](){(*task)();});
    }

    conditionVariable.notify_one();

    return res;
}



#endif