#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include "thread/block_queue.hpp"

#include <future>
#include <functional>

/** Thread Pool class
 *  while committing a task, if the task queue is full
 *  this commit will block until it can do this comit
 */
class ThreadPool
{
private:
    /** inner thread worker */
    class ThreadWorker
    {
    private:
        /** worker id */
        int _id;

        /** pool it belongs to */
        ThreadPool &_pool;

    public:
        /* constructor */
        ThreadWorker(ThreadPool &pool, const int id)
            : _pool(pool), _id(id) {}

        /* () operator */
        void operator()()
        {
            std::function<void()> func;
            bool dequeue;

            while (_pool._queue.available())
            {
                /** block if queue empty */
                dequeue = _pool._queue.dequeue(func);

                if (dequeue)
                    func();
            }
        }
    };

    /** threads pool */
    std::vector<std::thread> _threads;

    /* task queue */
    BlockQueue<std::function<void()>> _queue;

public:
    /** threads pool constructor */
    ThreadPool(const unsigned int n_threads, const unsigned int n_queue) : _threads(std::vector<std::thread>(n_threads)), _queue(n_queue)
    {
        for (int i = 0; i < _threads.size(); ++i)
        {
            _threads[i] = std::thread(ThreadWorker(*this, i));
        }
    }

    /** delete copy constructor */
    ThreadPool(const ThreadPool &) = delete;

    /** delete move contructor */
    ThreadPool(ThreadPool &&) = delete;

    /** delete assignment = */
    ThreadPool &operator=(const ThreadPool &) = delete;

    /** delete assignment = */
    ThreadPool &operator=(ThreadPool &&) = delete;

    /* Waits until threads finish their current task and shutdowns the pool */
    void shutdown()
    {
        _queue.stop();

        for (int i = 0; i < _threads.size(); ++i)
        {
            if (_threads[i].joinable())
            {
                _threads[i].join();
            }
        }
    }

    /** Submit a function to be executed asynchronously by the pool */
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        /** Create a function with bounded parameters ready to execute */
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        /** Encapsulate it into a shared ptr in order to be able to copy construct */
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        /** Wrap packaged task into void function */
        std::function<void()> wrapper_func = [task_ptr]()
        { (*task_ptr)(); };

        /** block if queue full */
        _queue.enqueue(wrapper_func);

        return task_ptr->get_future();
    }
};

#endif /* THREAD_POOL_HPP */
