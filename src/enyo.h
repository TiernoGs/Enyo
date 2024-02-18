
#ifndef ENYO_H
#define ENYO_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace enyo
{

    struct AABB
    {
        AABB(): pMin(0,0), pMax(0,0) {}
        AABB(std::pair<int64_t, int64_t> a, std::pair<int64_t, int64_t> b): pMin(a), pMax(b) {}

        bool empty() const { return pMin == pMax; }
        int64_t area() const { return (pMax.first - pMin.first) * (pMax.second - pMin.second); }
        std::pair<int64_t, int64_t> diagonal() const { return { pMax.first - pMin.first, pMax.second - pMin.second }; }
        AABB intersect(const AABB& aabb) const {
            AABB b;
            b.pMin.first = std::max(pMin.first, aabb.pMin.first);
            b.pMin.second = std::max(pMin.second, aabb.pMin.second);
            b.pMax.first = std::min(pMax.first, aabb.pMax.first);
            b.pMax.second = std::min(pMax.second, aabb.pMax.second);
            return b;
        }

        std::pair<int64_t, int64_t> pMin;
        std::pair<int64_t, int64_t> pMax;
    };

    // Parallel Function Declarations
    void Init(int64_t nThreads=-1);
    void Cleanup();

    int64_t AvailableCores();
    int64_t RunningThreads();
    uint32_t GetThreadIndex();


    // ThreadLocal Definition
    template <typename T>
    class ThreadLocal
    {
    public:
        // ThreadLocal Public Methods
        ThreadLocal() : hashTable(4 * RunningThreads()), create([]() { return T(); }) {}
        ThreadLocal(std::function<T(void)> &&c)
            : hashTable(4 * RunningThreads()), create(c) {}

        T &Get();

        template <typename F>
        void ForAll(F &&func);

    private:
        // ThreadLocal Private Members
        struct Entry
        {
            std::thread::id tID;
            T value;
        };
        std::shared_mutex mutex;
        std::vector<std::optional<Entry>> hashTable;
        std::function<T(void)> create;
    };

    // ThreadLocal Inline Methods
    template <typename T>
    inline T &ThreadLocal<T>::Get()
    {
        std::thread::id tid = std::this_thread::get_id();
        uint32_t hash = std::hash<std::thread::id>()(tid);
        hash %= hashTable.size();
        int32_t step = 1;
        int32_t tries = 0;

        mutex.lock_shared();
        while (true)
        {
            CHECK_LT(++tries, hashTable.size()); // full hash table

            if (hashTable[hash] && hashTable[hash]->tid == tid)
            {
                // Found it
                T &threadLocal = hashTable[hash]->value;
                mutex.unlock_shared();
                return threadLocal;
            }
            else if (!hashTable[hash])
            {
                mutex.unlock_shared();

                // Get reader-writer lock before calling the callback so that the user
                // doesn't have to worry about writing a thread-safe callback.
                mutex.lock();
                T newItem = create();

                if (hashTable[hash])
                {
                    // someone else got there first--keep looking, but now
                    // with a writer lock.
                    while (true)
                    {
                        hash += step;
                        ++step;
                        if (hash >= hashTable.size())
                            hash %= hashTable.size();

                        if (!hashTable[hash])
                            break;
                    }
                }

                hashTable[hash] = Entry{tid, std::move(newItem)};
                T &threadLocal = hashTable[hash]->value;
                mutex.unlock();
                return threadLocal;
            }

            hash += step;
            ++step;
            if (hash >= hashTable.size())
                hash %= hashTable.size();
        }
    }

    template <typename T>
    template <typename F>
    inline void ThreadLocal<T>::ForAll(F &&func)
    {
        mutex.lock();
        for (auto &entry : hashTable)
        {
            if (entry)
                func(entry->value);
        }
        mutex.unlock();
    }

    // Barrier Definition
    class Barrier
    {
    public:
        explicit Barrier(int64_t n) : numToBlock(n), numToExit(n) {}

        Barrier(const Barrier &) = delete;
        Barrier &operator=(const Barrier &) = delete;

        // All block. Returns true to only one thread (which should delete the
        // barrier).
        bool Block();

    private:
        std::mutex mutex;
        std::condition_variable cv;
        int64_t numToBlock;
        int64_t numToExit;
    };

    void ParallelFor(int64_t start, int64_t end, std::function<void(int64_t, int64_t)> func);
    void ParallelFor2D(const AABB& extent, std::function<void(AABB)> func);

    // Parallel Inline Functions
    inline void ParallelFor(int64_t start, int64_t end, std::function<void(int64_t)> func)
    {
        ParallelFor(start, end, [&func](int64_t start, int64_t end)
                    {
        for (int64_t i = start; i < end; ++i)
            func(i); });
    }

    inline void ParallelFor2D(const AABB &extent, std::function<void(std::pair<int64_t, int64_t>)> func)
    {
        ParallelFor2D(extent, [&func](AABB b)
        {
            for (int64_t pX = b.pMin.first; pX < b.pMax.first; pX++)
                for (int64_t pY = b.pMin.second; pY < b.pMax.second; pY++)
                func({pX, pY});
        });
    }

    class ThreadPool;

    // ParallelJob Definition
    class ParallelJob
    {
    public:
        // ParallelJob Public Methods
        virtual ~ParallelJob() {}

        virtual bool HaveWork() const = 0;
        virtual void RunStep(std::unique_lock<std::mutex> *lock) = 0;

        bool Finished() const { return !HaveWork() && activeWorkers == 0; }

        // ParallelJob Public Members
        static std::unique_ptr<ThreadPool> threadPool;

    private:
        // ParallelJob Private Members
        friend class ThreadPool;
        int32_t activeWorkers   = 0;
        ParallelJob *prev       = nullptr;
        ParallelJob *next       = nullptr;
        bool removed            = false;
    };

    // ThreadPool Definition
    class ThreadPool
    {
    public:
        // ThreadPool Public Methods
        explicit ThreadPool(int64_t nThreads);

        ~ThreadPool();

        size_t size() const { return threads.size(); }

        std::unique_lock<std::mutex> AddToJobList(ParallelJob *job);
        void RemoveFromJobList(ParallelJob *job);

        void WorkOrWait(std::unique_lock<std::mutex> *lock, bool isEnqueuingThread);
        bool WorkOrReturn();

        void Disable();
        void Reenable();

        void ForEachThread(std::function<void(void)> func);

    private:
        // ThreadPool Private Methods
        void Worker();

        // ThreadPool Private Members
        std::vector<std::thread> threads;
        mutable std::mutex mutex;
        ParallelJob *jobList        = nullptr;
        std::condition_variable jobListCondition;
        bool shutdownThreads        = false;
        bool disabled               = false;
    };

    bool DoParallelWork();

    // AsyncJob Definition
    template <typename T>
    class AsyncJob : public ParallelJob
    {
    public:
        // AsyncJob Public Methods
        AsyncJob(std::function<T(void)> w) : func(std::move(w)) {}

        bool HaveWork() const { return !started; }

        void RunStep(std::unique_lock<std::mutex> *lock)
        {
            threadPool->RemoveFromJobList(this);
            started = true;
            lock->unlock();
            // Execute asynchronous work and notify waiting threads of its completion
            T r = func();
            std::unique_lock<std::mutex> ul(mutex);
            result = r;
            cv.notify_all();
        }

        bool IsReady() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return result.has_value();
        }

        T GetResult()
        {
            Wait();
            std::lock_guard<std::mutex> lock(mutex);
            return *result;
        }

        std::optional<T> TryGetResult(std::mutex *extMutex)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (result)
                    return result;
            }

            extMutex->unlock();
            DoParallelWork();
            extMutex->lock();
            return {};
        }

        void Wait()
        {
            while (!IsReady() && DoParallelWork())
                ;
            std::unique_lock<std::mutex> lock(mutex);
            if (!result.has_value())
                cv.wait(lock, [this]()
                        { return result.has_value(); });
        }

        void DoWork()
        {
            T r = func();
            std::unique_lock<std::mutex> l(mutex);
            CHECK(!result.has_value());
            result = r;
            cv.notify_all();
        }

    private:
        // AsyncJob Private Members
        std::function<T(void)> func;
        bool started = false;
        std::optional<T> result;
        mutable std::mutex mutex;
        std::condition_variable cv;
    };

    void ForEachThread(std::function<void(void)> func);

    void DisableThreadPool();
    void ReenableThreadPool();

    // Asynchronous Task Launch Function Definitions
    template <typename F, typename... Args>
    inline auto RunAsync(F func, Args &&...args)
    {
        // Create _AsyncJob_ for _func_ and _args_
        auto fvoid = std::bind(func, std::forward<Args>(args)...);
        using R = typename std::invoke_result_t<F, Args...>;
        AsyncJob<R> *job = new AsyncJob<R>(std::move(fvoid));

        // Enqueue _job_ or run it immediately
        std::unique_lock<std::mutex> lock;
        if (RunningThreads() == 1)
            job->DoWork();
        else
            lock = ParallelJob::threadPool->AddToJobList(job);

        return job;
    }

} // namespace enyo

#endif //! ENYO_H
