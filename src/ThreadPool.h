#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

// A fixed set of worker threads for data-parallel loops. The workers sleep
// between loops, so a loop costs a wake-up instead of starting new threads.
class ThreadPool {
public:
    // Uses `threadCount` threads in total, the calling thread included.
    explicit ThreadPool(unsigned threadCount = std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Calls body(i) once for every i in [0, count) on up to `maxThreads`
    // threads and returns when all calls have finished. Indices are handed out
    // one at a time, so cores of different speed balance themselves.
    //
    // `body` must not throw, and loops must not be nested or started from
    // several threads at once.
    void parallelFor(int count, const std::function<void(int)>& body,
                     int maxThreads = std::numeric_limits<int>::max());

private:
    void workerLoop(std::stop_token stop);
    void runIndices();

    std::mutex m_mutex;
    std::condition_variable_any m_workAvailable;  // signaled when m_invitations is raised
    std::condition_variable m_workersDone;        // signaled when m_busyWorkers drops to zero

    // The loop in progress. Written under m_mutex before workers are invited.
    const std::function<void(int)>* m_body = nullptr;
    int m_count = 0;
    std::atomic<int> m_nextIndex{0};

    int m_invitations = 0;  // workers the current loop still has room for
    int m_busyWorkers = 0;  // workers currently inside runIndices()

    // Declared last: jthread stops and joins on destruction, which must happen
    // while the members above are still alive.
    std::vector<std::jthread> m_workers;
};
