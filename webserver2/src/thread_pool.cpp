#include <iostream>

#include "thread_pool.h"

/**
     * @brief 构造函数。创建并启动指定数量的工作线程。
     * * @param threads 要创建的工作线程数量。
     */
ThreadPool::ThreadPool(size_t threads) : stop_flag(false) {
    // 创建指定数量的工作线程
    for (size_t i = 0; i < threads; i++)
    {
        // 使用 lambda 表达式定义线程执行的任务
        workers.emplace_back([this] {
            // 每个线程不断循环, 等待并执行任务
            while(true) {
                std::function<void()> task; // 创建一个函数包装模板变量 void() 表示函数返回值为空参数为空
                {
                    // 生产者-消费者模型的核心等待逻辑

                    // 线程锁 std::mutex标识互斥锁 std::unique_lock管理互斥锁上锁与解锁, 这里是上锁
                    std::unique_lock<std::mutex> lock(this->queue_mutex);

                    // 线程在这里阻塞等待两个条件之一成立：
                    //  stop_flag 为真(线程池准备停止), 或 tasks 队列非空(有任务要执行)
                    this->condition.wait(lock, [this]{
                        // 等待条件：线程停止 或 任务队列非空
                        return this->stop_flag || !this->tasks.empty();
                    });

                    if(this->stop_flag && this->tasks.empty())  {
                        return; // 线程退出
                    }

                    task = std::move(this->tasks.front());  // 获取队列头部任务
                    this->tasks.pop();                      // 弹出队列头部任务
                }

                try {
                    task(); // 执行任务
                }catch (const std::exception& e) {
                    std::cerr << "Worker thread caught exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Worker thread caught unknown exception" << std::endl;
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_flag = true;
    }

    // 作用：唤醒所有当前正在 condition.wait() 上休眠等待任务的线程。
    // 如果 stop_flag 是 false，它们会被唤醒并检查队列是否有任务。
    // 由于 stop_flag 已经被设置为 true，被唤醒的线程会检查它们的退出条件
    condition.notify_all();

    // 调用线程的 join() 方法，阻塞析构线程（通常是主线程）的执行，直到对应的 worker 线程执行完毕并终止
    for (auto& worker : workers) {
        worker.join();
    }
}