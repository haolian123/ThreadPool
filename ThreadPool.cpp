#include"ThreadPool.h"
#include<iostream>

//工作线程的函数
void ThreadPool::work(std::size_t workerNumber){
    while(true){
        std::function<void()>task;
        bool notify;
        {
            //获取队列锁并等待条件变量满足
            std::unique_lock<std::mutex>lock(tasksQueueMutex);
            conditionConsumers.wait(lock,
                [this,workerNumber]
                {
                    return this->threadPoolStop||
                    !this->tasksQueue.empty()||
                    poolSize<workerNumber+1;
                }
            );
            
            //处理线程池缩减或关闭
            if((this->threadPoolStop&&this->tasksQueue.empty())||
                (!this->threadPoolStop&&poolSize<workerNumber+1)){
                    //分离这个线程
                    this->workers[workerNumber].detach();

                    //减少工作线程的数量
                    while(this->workers.size()>poolSize&&!this->workers.back().joinable()){
                        this->workers.pop_back();
                    }

                    //如果工作线程为空,唤醒析构函数中等待的条件
                    if(this->workers.empty()){
                        this->conditionConsumers.notify_all();
                    }
                    return ;
                    
                    
            }else if(!this->tasksQueue.empty()){
                task=std::move(this->tasksQueue.front());
                this->tasksQueue.pop();
                notify=this->tasksQueue.size()+1==maxQueueSize||this->tasksQueue.empty();
            }else
                continue;
                

        }
        //减少正在执行的任务数量
        HandleInFlightDecrement guard(*this);

        //如果符合通知生产任务的条件
        if(notify){
            std::unique_lock<std::mutex> lock(this->tasksQueueMutex);
            conditionProducers.notify_all();
        }

        //**捕获并处理异常**
        try {
            // 执行任务
            task();  
        } catch (const std::exception& ex) {
            // 处理异常，可以记录日志
            std::cerr << "Exception in thread: " << ex.what() << std::endl;
        }
    }
}

//启动工作线程
void ThreadPool::startWorker(std::size_t workerNumber, std::unique_lock<std::mutex>const &lock){
    //确保当前线程持有队列锁并且锁的指针指向当前线程池的队列锁
    assert(lock.owns_lock()&&lock.mutex()==&this->tasksQueueMutex);
    //确保工作线程数量不超过工作线程队列的大小
    assert(workerNumber<=this->workers.size());

    if(workerNumber<this->workers.size()){
        std::thread & worker=this->workers[workerNumber];
        //如果线程可加入，则启动线程
        if(!worker.joinable()){
            
            worker=std::thread([this,workerNumber]{work(workerNumber);});
        }
    }else{
        // 将新的工作线程加入到工作线程向量中
        this->workers.emplace_back([this,workerNumber]{work(workerNumber);});
    }

}

ThreadPool::ThreadPool(size_t threadsNumber):poolSize(threadsNumber),inFlight(0){
    //把工作函数加入工作线程数组中
    std::unique_lock<std::mutex>lock(this->tasksQueueMutex);
    for(size_t i=0;i<threadsNumber;i++){
        startWorker(i,lock);
    }

}
// 设置任务队列大小的上限
void ThreadPool::setQueueSizeLimit(std::size_t limit){
    // 创建一个互斥锁来保护任务队列
    std::unique_lock<std::mutex> lock(this->tasksQueueMutex);
    
    // 如果线程池已停止，直接返回
    if(threadPoolStop){
        return;
    }
    
    // 保存旧的队列大小上限
    std::size_t const oldLimit = maxQueueSize;
    
    // 更新队列大小上限，至少为 1
    maxQueueSize = std::max(limit, std::size_t(1));
    
    // 如果新的队列大小大于旧的大小，通知所有生产者线程
    if(oldLimit < maxQueueSize){
        conditionProducers.notify_all();
    }
}

// 设置线程池的大小
void ThreadPool::setPoolSize(std::size_t limit){
    // 线程池大小至少为 1
    if(limit < 1){
        limit = 1;
    }

    // 创建一个互斥锁来保护任务队列
    std::unique_lock<std::mutex> lock(this->tasksQueueMutex);
    
    // 如果线程池已停止，直接返回
    if(threadPoolStop)
        return;
    
    // 保存旧的线程池大小
    std::size_t const oldSize = poolSize;
    
    // 断言：当前工作线程的数量应不小于旧的线程池大小
    assert(this->workers.size() >= oldSize);
    
    // 更新线程池大小
    poolSize = limit;
    
    if(poolSize > oldSize){
        // 创建新的工作线程
        // 注意：即使池大小减小，这些新线程也会继续运行
        for(std::size_t i = oldSize; i != poolSize; i++){
            startWorker(i, lock);
        }
    } else if(poolSize < oldSize){
        // 通知所有工作线程，线程池将开始缩小
        this->conditionConsumers.notify_all();
    }
}

// 等待任务队列变为空
void ThreadPool::waitUntilTasksQueueIsEmpty(){
    // 创建一个互斥锁来保护任务队列
    std::unique_lock<std::mutex> lock(this->tasksQueueMutex);
    
    // 等待条件变量，直到任务队列为空
    this->conditionProducers.wait(lock,
        [this]{ return this->tasksQueue.empty(); }    
    );
}

// 等待所有任务完成
void ThreadPool::waitUntilNothingInFlight(){
    // 创建一个互斥锁来保护“inFlight”变量
    std::unique_lock<std::mutex> lock(this->inFlightMutex);
    
    // 等待条件变量，直到所有任务都完成（即 inFlight 变为 0）
    this->inFlightCondition.wait(lock,
        [this]{ return this->inFlight == 0; }
    );
}


ThreadPool::~ThreadPool(){

    //获取任务队列的锁
    std::unique_lock<std::mutex>lock(tasksQueueMutex);
    //设置线程池停止
    threadPoolStop=true;
    //线程池大小设置为0
    poolSize=0;
    //唤醒所有消费者线程
    conditionConsumers.notify_all();
    //唤醒所有生产者线程
    conditionProducers.notify_all();
    //等待队列为空
    conditionConsumers.wait(lock,[this]{return this->workers.empty();});
    //检查所有任务都释放
    assert(inFlight==0);
}