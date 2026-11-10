#include "ThreadPool.h"

#include <algorithm>

ThreadPool::ThreadPool(unsigned threadCount) {
    // hardware_concurrency() may report 0 when it cannot tell.
    const unsigned workerCount = std::max(threadCount, 1u) - 1;  // the caller is a thread too
    m_workers.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i)
        m_workers.emplace_back(std::bind_front(&ThreadPool::workerLoop, this));
}

void ThreadPool::parallelFor(int count, const std::function<void(int)>& body, int maxThreads) {
    const int workerCount = static_cast<int>(m_workers.size());
    const int helpers = std::clamp(std::min(count, maxThreads) - 1, 0, workerCount);
    {
        const std::scoped_lock lock(m_mutex);
        m_body = &body;
        m_count = count;
        m_nextIndex.store(0, std::memory_order_relaxed);
        m_invitations = helpers;
    }
    if (helpers == workerCount) {
        m_workAvailable.notify_all();
    } else {
        for (int i = 0; i < helpers; ++i)
            m_workAvailable.notify_one();
    }

    runIndices();  // the calling thread takes its share

    // Every index has been claimed by now, so workers that have not woken up
    // yet are no longer needed; wait only for those still running a call.
    std::unique_lock lock(m_mutex);
    m_invitations = 0;
    m_workersDone.wait(lock, [this] { return m_busyWorkers == 0; });
}

void ThreadPool::runIndices() {
    for (;;) {
        const int index = m_nextIndex.fetch_add(1, std::memory_order_relaxed);
        if (index >= m_count) return;
        (*m_body)(index);
    }
}

void ThreadPool::workerLoop(std::stop_token stop) {
    std::unique_lock lock(m_mutex);
    // wait() returns false once the pool is being destroyed.
    while (m_workAvailable.wait(lock, stop, [this] { return m_invitations > 0; })) {
        --m_invitations;
        ++m_busyWorkers;
        lock.unlock();
        runIndices();
        lock.lock();
        if (--m_busyWorkers == 0) m_workersDone.notify_one();
    }
}
