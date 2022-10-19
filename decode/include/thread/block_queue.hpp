#ifndef BLOCK_QUEUE_HPP
#define BLOCK_QUEUE_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

template <typename T>
class BlockQueue
{
private:
    std::mutex _mt;
    std::condition_variable _cv_con;
    std::condition_variable _cv_prod;
    std::queue<T>   _tasks;
    std::atomic<bool> _stopped;

    const unsigned int _capacity;

    bool stopped() 
    {
        return _stopped.load();
    }

    bool empty() 
    {
        return _tasks.size() == 0 ? true : false;
    }

    bool full()
    {
        return _tasks.size() == _capacity ? true : false;
    }
    
public:
    BlockQueue(const unsigned int capacity) : _capacity(capacity), _stopped(false) {}

    BlockQueue()
    {
        stop();
        _cv_con.notify_all();
        _cv_prod.notify_all();
    }

    void stop()
    {
        _stopped.store(true);
        _cv_con.notify_all();
    }

    bool available()
    {
        return !stopped() || !empty();
    }

    void enqueue(T& data) {
        std::unique_lock<std::mutex> _lck(_mt);
        while (full()) 
        {
            _cv_con.notify_one();
            _cv_prod.wait(_lck);
        }
        _tasks.push(data);
        _cv_con.notify_one();
    }

    bool dequeue(T& data) {
        std::unique_lock<std::mutex> _lck(_mt);
        while (empty()) {
            if (this->stopped()) 
                return false;

            _cv_prod.notify_one();
            _cv_con.wait(_lck, [this]() { return this->stopped() || !this->empty(); });
        }

        data = _tasks.front();
        _tasks.pop();
        _cv_prod.notify_one();
        return true;
    }
};

#endif /* BLOCK_QUEUE_HPP */
