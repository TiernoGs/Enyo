
#include "enyo.h"

#include <algorithm>
#include <iterator>
#include <list>
#include <thread>
#include <vector>

#if defined(_DEBUG) || defined(ENYO_FORCE_DEBUG) 
  #define ENYO_DEBUG_BUILD
#endif

#ifdef ENYO_OSX_PLATFORM
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif
#ifdef ENYO_LINUX_PLATFORM
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif
#ifdef ENYO_WINDOWS_PLATFORM
#include <windows.h>
#endif

namespace enyo
{

    uint32_t GetThreadIndex() {
#ifdef ENYO_LINUX_PLATFORM
        unsigned int tid = syscall(SYS_gettid);
        return tid;
#elif defined(ENYO_WINDOWS_PLATFORM)
        return GetCurrentThreadId();
#elif defined(ENYO_OSX_PLATFORM)
        uint64_t tid;
        return tid;
#else
#error "Need to define GetThreadIndex() for system"
#endif
    }

    // Barrier Method Definitions
    bool Barrier::Block()
    {
        std::unique_lock<std::mutex> lock(mutex);

        --numToBlock;
        if (numToBlock > 0)
        {
            cv.wait(lock, [this]()
                    { return numToBlock == 0; });
        }
        else
            cv.notify_all();

        return --numToExit == 0;
    }

    std::unique_ptr<ThreadPool> ParallelJob::threadPool;

    // ThreadPool Method Definitions
    ThreadPool::ThreadPool(int64_t nThreads)
    {
        for (int64_t i = 0; i < nThreads - 1; ++i)
            threads.push_back(std::thread(&ThreadPool::Worker, this));
    }

    void ThreadPool::Worker()
    {
#ifdef ENYO_DEBUG_BUILD
        printf("Started execution in worker thread with ID #%d\n", GetThreadIndex());
#endif

        std::unique_lock<std::mutex> lock(mutex);
        while (!shutdownThreads)
            WorkOrWait(&lock, false);

#ifdef ENYO_DEBUG_BUILD
        printf("Exiting worker thread with ID #%d\n",GetThreadIndex());
#endif
    }

    std::unique_lock<std::mutex> ThreadPool::AddToJobList(ParallelJob *job)
    {
        std::unique_lock<std::mutex> lock(mutex);
        // Add _job_ to head of _jobList_
        if (jobList)
            jobList->prev = job;
        job->next = jobList;
        jobList = job;

        jobListCondition.notify_all();
        return lock;
    }

    void ThreadPool::WorkOrWait(std::unique_lock<std::mutex> *lock, bool isEnqueuingThread)
    {
        // Return if this is a worker thread and the thread pool is disabled
        if (!isEnqueuingThread && disabled)
        {
            jobListCondition.wait(*lock);
            return;
        }

        ParallelJob *job = jobList;
        while (job && !job->HaveWork())
            job = job->next;
        if (job)
        {
            // Execute work for _job_
            job->activeWorkers++;
            job->RunStep(lock);
            // Handle post-job-execution details
            lock->lock();
            job->activeWorkers--;
            if (job->Finished())
                jobListCondition.notify_all();
        }
        else
            // Wait for new work to arrive or the job to finish
            jobListCondition.wait(*lock);
    }

    void ThreadPool::RemoveFromJobList(ParallelJob *job)
    {
        if (job->prev)
            job->prev->next = job->next;
        else
            jobList = job->next;

        if (job->next)
            job->next->prev = job->prev;

        job->removed = true;
    }

    bool ThreadPool::WorkOrReturn()
    {
        std::unique_lock<std::mutex> lock(mutex);

        ParallelJob *job = jobList;
        while (job && !job->HaveWork())
            job = job->next;
        if (!job)
            return false;

        // Execute work for _job_
        job->activeWorkers++;
        job->RunStep(&lock);
        lock.lock();
        job->activeWorkers--;
        if (job->Finished())
            jobListCondition.notify_all();

        return true;
    }

    void ThreadPool::ForEachThread(std::function<void(void)> func)
    {
        std::unique_ptr<Barrier> b = std::make_unique<Barrier>(threads.size() + 1);

        ParallelFor(0, threads.size() + 1, [&b, &func](int64_t)
        {
            func();
            if (b->Block())
                b.reset();
        });
    }

    void ThreadPool::Disable()
    {
        disabled = true;
    }

    void ThreadPool::Reenable()
    {
        disabled = false;
    }

    ThreadPool::~ThreadPool()
    {
        if (threads.empty())
            return;

        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdownThreads = true;
            jobListCondition.notify_all();
        }

        for (std::thread &thread : threads)
            thread.join();
    }

    bool DoParallelWork()
    {
        // lock should be held when this is called...
        return ParallelJob::threadPool->WorkOrReturn();
    }

    // ParallelForLoop1D Definition
    class ParallelForLoop1D : public ParallelJob
    {
    public:
        // ParallelForLoop1D Public Methods
        ParallelForLoop1D(int64_t startIndex, int64_t endIndex, int32_t chunkSize,
                          std::function<void(int64_t, int64_t)> func)
            : func(std::move(func)),
              nextIndex(startIndex),
              endIndex(endIndex),
              chunkSize(chunkSize) {}

        bool HaveWork() const { return nextIndex < endIndex; }

        void RunStep(std::unique_lock<std::mutex> *lock);

    private:
        // ParallelForLoop1D Private Members
        std::function<void(int64_t, int64_t)> func;
        int64_t nextIndex;
        int64_t endIndex;
        int32_t chunkSize;
    };

    class ParallelForLoop2D : public ParallelJob
    {
    public:
        ParallelForLoop2D(const AABB &extent, int32_t chunkSize,
                          std::function<void(AABB)> func)
            : func(std::move(func)),
              extent(extent),
              nextStart(extent.pMin),
              chunkSize(chunkSize) {}

        bool HaveWork() const { return nextStart.second < extent.pMax.second; }
        void RunStep(std::unique_lock<std::mutex> *lock);

    private:
        std::function<void(AABB)> func;
        const AABB extent;
        std::pair<int64_t, int64_t> nextStart;
        int32_t chunkSize;
    };

    // ParallelForLoop1D Method Definitions
    void ParallelForLoop1D::RunStep(std::unique_lock<std::mutex> *lock)
    {
        // Determine the range of loop iterations to run in this step
        int64_t indexStart = nextIndex;
        int64_t indexEnd = std::min(indexStart + chunkSize, endIndex);
        nextIndex = indexEnd;

        // Remove job from list if all work has been started
        if (!HaveWork())
            threadPool->RemoveFromJobList(this);

        // Release lock and execute loop iterations in _[indexStart, indexEnd)_
        lock->unlock();
        func(indexStart, indexEnd);
    }

    void ParallelForLoop2D::RunStep(std::unique_lock<std::mutex> *lock)
    {
        // Compute extent for this step
        std::pair<int64_t, int64_t> end = {nextStart.first + chunkSize, nextStart.second + chunkSize};
        AABB b = AABB(nextStart, end).intersect(extent);

        // Advance to be ready for the next extent.
        nextStart.first += chunkSize;
        if (nextStart.first >= extent.pMax.first)
        {
            nextStart.first = extent.pMin.first;
            nextStart.second += chunkSize;
        }

        if (!HaveWork())
            threadPool->RemoveFromJobList(this);

        lock->unlock();

        // Run the loop iteration
        func(b);
    }

    // Parallel Function Definitions
    void ParallelFor(int64_t start, int64_t end, std::function<void(int64_t, int64_t)> func)
    {
        if (start == end)
            return;
        // Compute chunk size for parallel loop
        int32_t chunkSize = std::max<int32_t>(1, int32_t((end - start) / (8 * RunningThreads())) );

        // Create and enqueue _ParallelForLoop1D_ for this loop
        ParallelForLoop1D loop(start, end, chunkSize, std::move(func));
        std::unique_lock<std::mutex> lock = ParallelJob::threadPool->AddToJobList(&loop);

        // Help out with parallel loop iterations in the current thread
        while (!loop.Finished())
            ParallelJob::threadPool->WorkOrWait(&lock, true);
    }

    void ParallelFor2D(const AABB &extent, std::function<void(AABB)> func)
    {
        if (extent.empty())
            return;
        if (extent.area() == 1)
        {
            func(extent);
            return;
        }

        // Want at least 8 tiles per thread, subject to not too big and not too
        // small.
        int32_t tileSize = std::clamp(int32_t(std::sqrt(extent.diagonal().first * extent.diagonal().second /
                                                   (8 * RunningThreads()))),
                                 1, 32);

        ParallelForLoop2D loop(extent, tileSize, std::move(func));
        std::unique_lock<std::mutex> lock = ParallelJob::threadPool->AddToJobList(&loop);

        // Help out with parallel loop iterations in the current thread
        while (!loop.Finished())
            ParallelJob::threadPool->WorkOrWait(&lock, true);
    }

    ///////////////////////////////////////////////////////////////////////////

    int64_t AvailableCores()
    {
        return std::max<int64_t>(1, std::thread::hardware_concurrency());
    }

    int64_t RunningThreads()
    {
        return ParallelJob::threadPool ? (1 + ParallelJob::threadPool->size()) : 1;
    }

    void Init(int64_t nThreads)
    {
        if (nThreads <= 0)
            nThreads = AvailableCores();
        ParallelJob::threadPool = std::make_unique<ThreadPool>(nThreads);
    }

    void Cleanup()
    {
        ParallelJob::threadPool.reset();
    }

    void ForEachThread(std::function<void(void)> func)
    {
        if (ParallelJob::threadPool)
            ParallelJob::threadPool->ForEachThread(std::move(func));
    }

    void DisableThreadPool()
    {
        ParallelJob::threadPool->Disable();
    }

    void ReenableThreadPool()
    {
        ParallelJob::threadPool->Reenable();
    }

} // namespace enyo
