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
void ThreadPool::setQueueSizeLimit(std::size_t limit){
    std::unique_lock<std::mutex>lock(this->tasksQueueMutex);
    
    if(threadPoolStop){
        return;
    }
    std::size_t const oldLimit=maxQueueSize;
    maxQueueSize=std::max(limit,std::size_t(1));
    if(oldLimit<maxQueueSize){
        conditionProducers.notify_all();
    }
}
void ThreadPool::setPoolSize(std::size_t limit){
    if(limit<1){
        limit=1;
    }

    std::unique_lock<std::mutex>lock(this->tasksQueueMutex);
    if(threadPoolStop)
        return;
    
    std::size_t const oldSize=poolSize;
    assert(this->workers.size()>=oldSize);

    poolSize=limit;
    if(poolSize>oldSize){
        //创建新的工作线程
        //有可能在池大小减小后仍在运行，这样的线程将继续运行
        for(std::size_t i=oldSize;i!=poolSize;i++){
            startWorker(i,lock);
        }
    }else if(poolSize<oldSize){
        // 通知所有工作线程开始缩小
        this->conditionConsumers.notify_all();
    }
}

//等待工作队列为空
void ThreadPool::waitUntilTasksQueueIsEmpty(){
    std::unique_lock<std::mutex> lock(this->tasksQueueMutex);
    this->conditionProducers.wait(lock,
        [this]{return this->tasksQueue.empty();}    
    );
}

//等待所有任务完成
void ThreadPool::waitUntilNothingInFlight(){
    std::unique_lock<std::mutex> lock(this->inFlightMutex);
    this->inFlightCondition.wait(lock,
        [this]{return this->inFlight==0;}
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