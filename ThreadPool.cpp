#include"ThreadPool.h"
#include<iostream>
void ThreadPool::work(){
    while(true){
        std::function<void()>task;
        {
            std::unique_lock<std::mutex>lock(tasksQueueMutex);
            conditionVariable.wait(lock,
                [this]{return this->threadPoolStop||!this->tasksQueue.empty();}
            );
            if(this->threadPoolStop&&this->tasksQueue.empty())
                return;
            task=std::move(tasksQueue.front());
            tasksQueue.pop();
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


ThreadPool::ThreadPool(size_t threadsNumber){
    //把工作函数加入工作线程数组中
    for(size_t i=0;i<threadsNumber;i++){
        this->workers.emplace_back([this]{work();});
    }
}




ThreadPool::~ThreadPool(){

    {
        std::unique_lock<std::mutex>lock(tasksQueueMutex);
        threadPoolStop=true;
    }
    conditionVariable.notify_all();
    for(auto&worker:workers){
        worker.join();
    }
}