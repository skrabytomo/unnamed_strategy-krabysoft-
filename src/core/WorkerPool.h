#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

// Persistent worker pool — THREADING.md Phase 2.
//
// Created ONCE (a static singleton): spawning threads per turn costs more than
// the parallelism buys back, which is why the design calls for a persistent
// pool rather than std::async at the call site.
//
// Only exposes a *blocking* parallelFor. That is deliberate: it makes the call
// site a drop-in replacement for a serial loop, so no caller has to reason
// about lifetime or completion. The dispatching thread also runs work itself,
// so a pool of N threads gives N+1 workers and never idles the caller.
//
// Safety contract for callers: fn(i) must only READ shared state, or write to
// memory owned exclusively by index i. Phase 2's candidate A* fan-out satisfies
// this — Pathfinder::find is pure and costFn only reads the map/towns/roads.
class WorkerPool
{
public:
    static WorkerPool& instance()
    {
        static WorkerPool pool;
        return pool;
    }

    int workerCount() const { return static_cast<int>(m_threads.size()) + 1; }

    // Runs fn(i) for every i in [0, n). Blocks until all have completed.
    // Falls back to a plain serial loop for tiny n or a degenerate pool, so
    // behaviour is identical either way.
    void parallelFor(int n, const std::function<void(int)>& fn)
    {
        if (n <= 0) return;
        if (n == 1 || m_threads.empty()) {
            for (int i = 0; i < n; ++i) fn(i);
            return;
        }

        auto job = std::make_shared<Job>();
        job->fn        = &fn;
        job->total     = n;
        job->next      = 0;
        job->remaining = n;

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            // One wake-up per worker; each drains indices until the job is done.
            for (size_t w = 0; w < m_threads.size(); ++w) m_jobs.push(job);
        }
        m_cvWork.notify_all();

        drain(*job);            // the caller pulls its share too

        std::unique_lock<std::mutex> lk(job->doneMutex);
        job->cvDone.wait(lk, [&job] { return job->remaining.load() == 0; });
    }

private:
    struct Job
    {
        const std::function<void(int)>* fn = nullptr;
        int                             total = 0;
        std::atomic<int>                next{0};
        std::atomic<int>                remaining{0};
        std::mutex                      doneMutex;
        std::condition_variable         cvDone;
    };

    WorkerPool()
    {
        unsigned hw = std::thread::hardware_concurrency();
        // Leave a core for the render thread; the caller is a worker too.
        int count = (hw > 2u) ? static_cast<int>(hw) - 1 : 1;
        for (int i = 0; i < count; ++i)
            m_threads.emplace_back([this] { workerLoop(); });
    }

    ~WorkerPool()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stop = true;
        }
        m_cvWork.notify_all();
        for (auto& t : m_threads)
            if (t.joinable()) t.join();
    }

    void workerLoop()
    {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cvWork.wait(lk, [this] { return m_stop || !m_jobs.empty(); });
                if (m_stop) return;
                job = m_jobs.front();
                m_jobs.pop();
            }
            drain(*job);
        }
    }

    // Pull indices until exhausted, then signal if we finished the last one.
    static void drain(Job& job)
    {
        for (;;) {
            int i = job.next.fetch_add(1, std::memory_order_relaxed);
            if (i >= job.total) break;
            (*job.fn)(i);
            if (job.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(job.doneMutex);
                job.cvDone.notify_all();
            }
        }
    }

    std::vector<std::thread>          m_threads;
    std::queue<std::shared_ptr<Job>>  m_jobs;
    std::mutex                        m_mutex;
    std::condition_variable           m_cvWork;
    bool                              m_stop = false;
};
