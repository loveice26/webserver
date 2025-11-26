#pragma once

#include <vector>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <future>

// ======================== 线程池 ========================

/**
 * @brief 简单线程池实现，用于并发执行任务。
 * * 实现了经典的生产者-消费者模型，使用 std::condition_variable
 * 来管理工作线程的休眠和唤醒。
 */
class ThreadPool {
private:
    std::vector<std::thread> workers;           ///< 工作线程集合
    std::queue<std::function<void()>> tasks;    ///< 任务队列

    std::mutex queue_mutex;                      ///< 保护任务队列的互斥锁
    std::condition_variable condition;          ///< 用于等待/通知任务的条件变量
    bool stop_flag;                             ///< 线程池停止标志

public:
    ThreadPool(size_t n);   // 构造函数, 创建一个线程池
    ~ThreadPool();

    // class... Args 是一个模板参数包, 用来接收 任意数量、任意类型的参数
    template<class F, class... Args>
    // F&& f 是一个 右值引用, 但在模板上下文中，它通常是 "万能引用" 也叫 "转发引用", 这里 f 一般都是添函数名
    // "-> std::future<typename std::invoke_result<F, Args...>::type>"：这是 C++11/14 的尾随返回类型语法。
    //      - std::invoke_result<F, Args...>::type：用于在编译期推断出函数 F 以参数 Args... 调用时所产生的返回值类型。
    //      - 整个函数返回一个 std::future<返回类型>，这是客户端用来异步获取结果的关键。
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        // 通过 std::invoke_result 推断出函数 f 以及参数 args... 调用时的返回类型, 并定义一个类型别名 return_type
        using return_type = typename std::invoke_result<F, Args...>::type;

        // 包装任务为 packaged_task<return_type()>
        // auto task = std::make_shared<std::packaged_task<return_type()>>(
        //     std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        // );

        // 现代写使用 lambda 替代 bind
        // std::packaged_task<return_type()>：这是一个封装了可调用对象（这里是一个 Lambda 表达式）的类，它的特殊之处在于，它内部自动包含了一个 std::promise 对象。
        //  - 当 (*task)() 被执行时，它会自动调用内部的可调用对象，并将结果（或异常）通过其内部的 std::promise 写入。
        //  - 客户端通过 std::future 可以异步获取这个结果。
        // Lambda:
        //  - "[func = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable { ... }"：使用 C++14/17 的通用捕获和包展开语法，
        //      确保函数 f 和参数 args 都被完美转发并正确捕获到 Lambda 内部。mutable 确保捕获的副本可以在 Lambda 内部被移动或修改。
        //  - "return func(std::move(args)...);"：调用捕获的函数 f，并将参数 args 完美转发进去，返回其结果。
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                return func(std::move(args)...);
            });

        // 获取与任务相关的 future 对象, 供调用者来获取结果
        std::future<return_type> res = task->get_future();

        {
            // 这行代码创建了一个 std::unique_lock 对象，并尝试锁定互斥量 queue_mutex
            // 确保在同一时间，只有一个线程可以访问和修改线程池的共享资源（即任务队列 tasks 和停止标志 stop_flag）。这是实现线程安全的关键
            std::unique_lock<std::mutex> lock(queue_mutex);

            if(stop_flag)   // 指示线程已经关闭, 抛出错误异常
                throw std::runtime_error("enqueue on stopped ThreadPool");

            // tasks: 这是线程池内部的任务队列 (std::queue)，存储着待执行的 std::function<void()> 对象。
            // emplace: 优先使用 emplace 成员函数，因为它可以在容器内部直接就地构造新元素（这里是 std::function<void()>），避免了创建临时对象和随后的拷贝/移动操作，效率更高。
            // [task](){ (*task)(); }: 这是一个 Lambda 表达式。它通过值捕获了 std::packaged_task 的共享指针 task，并定义了工作线程将执行的实际操作：调用封装好的异步任务 (*task)()。
            tasks.emplace([task](){ (*task)(); });
        }

        // condition.notify_one();: 当任务成功放入队列后，需要通知等待中的工作线程。
        // condition 是一个 std::condition_variable。工作线程在队列为空时会使用 wait() 阻塞休眠
        // notify_one() 会唤醒一个正在等待的工作线程，告诉它“有新任务来了，快去执行”。
        // 注意：notify_one() 发生在锁释放（即到达 {...} 块末尾）之后，以避免不必要的上下文切换竞争。
        condition.notify_one();

        // return res;: 将之前获取的 std::future 对象 res 返回给调用者。这个 future 是调用者异步获取任务结果的凭证
        return res;
    }
};