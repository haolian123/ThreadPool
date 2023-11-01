#ifndef THREAD_POOL
#define THREAD_POOL
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include<atomic>
#include<cassert>
#include <condition_variable>
#include <future>
#include <algorithm>
#include <functional>
#include <stdexcept>

class ThreadPool{
public:
    //构造函数，传入工作线程数量
    ThreadPool(size_t threadsNumber);

    //将函数添加到工作线程中
    template<class F,class... Args>
    std::future<typename std::result_of<F(Args...)>::type> addTasksQueue(F&& f, Args&&... args) ;
    
    //等待直到任务队列为空
    void waitUntilTasksQueueIsEmpty();

    //等待直到没有任务在执行
    void waitUntilNothingInFlight();

    //析构函数，关闭线程池，释放所有线程
    ~ThreadPool();

    //设置线程池的大小
    void setPoolSize(std::size_t limit);

    //设置任务队列的大小限制
    void setQueueSizeLimit(std::size_t limit);

private:
    //工作线程数组
    std::vector<std::thread>workers;

    //任务数组
    std::queue<std::function<void()>>tasksQueue;

    //用于同步的互斥锁
    std::mutex tasksQueueMutex;//任务队列互斥锁
    std::condition_variable conditionProducers;//生产者条件变量
    std::condition_variable conditionConsumers;//消费者条件变量

    //线程池的限制大小
    std::size_t poolSize;

    //任务队列的最大长度限制
    std::size_t maxQueueSize=100000;

    //条件变量
    std::condition_variable conditionVariable;

    //工作函数
    void work(std::size_t workerNumber);
    void startWorker(std::size_t workerNumber,std::unique_lock<std::mutex>const &lock);

    //停止信号,用于关闭线程池
    bool threadPoolStop=false;

    //用于跟踪任务执行状态的互斥锁和条件变量
    std::mutex inFlightMutex;
    std::condition_variable inFlightCondition;
    std::atomic<std::size_t> inFlight; //原子变量

    //用于处理任务执行状态的自动减少的结构体
    struct HandleInFlightDecrement{
        ThreadPool &threadPool;
        // 初始化线程池
        HandleInFlightDecrement(ThreadPool& threadPool):threadPool(threadPool){}
        ~HandleInFlightDecrement(){
            //减少任务执行状态计数，使用原子操作
            std::size_t prev=std::atomic_fetch_sub_explicit(
                &threadPool.inFlight,
                std::size_t(1),
                std::memory_order_acq_rel
            );
            //如果减少前计数为1，表示没有执行的任务
            if(prev==1){
                std::unique_lock<std::mutex>guard(threadPool.inFlightMutex);
                //通知等待的线程，所有任务都已经执行完毕
                threadPool.inFlightCondition.notify_all();
            }
        }
    };

    
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

        //检查是否当前等待队列的大小是否超过了队列最大限制
        if(tasksQueue.size()>=this->maxQueueSize){
            //等待一些任务被完成或线程池停止
            conditionProducers.wait(lock,
                [this]{
                    return tasksQueue.size()<maxQueueSize||threadPoolStop;
                }
            );
        }

        // conditionVariable.wait(lock);
        if(threadPoolStop){
            throw std::runtime_error("线程池已停止！");
        }
        tasksQueue.emplace([task](){(*task)();});
    }

    //将任务执行状态计数加1
    std::atomic_fetch_add_explicit(&inFlight,std::size_t(1),std::memory_order_relaxed);
    
    //通知一个线程来处理任务队列中的任务
    conditionConsumers.notify_one();

    return res;
}



#endif