#pragma once
#include<queue>
#include<thread>
#include<mutex>
#include<condition_variable>

// 异步写日志队列
template<typename T>
class LockQueue 
{
public:
    void Push(const T &data)
    {
        std::lock_guard<std::mutex>lock(m_mutex);
        m_queue.push(data);
        m_condvariable.notify_one();
    }
    T Pop()
    {
        std::unique_lock<std::mutex>lock(m_mutex);
        // 如果未空，在wait阻塞，但是把锁释放掉，一直等notify后，再重新检测，判断是否为空
        m_condvariable.wait(lock,[this](){return !m_queue.empty();});
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condvariable;
};