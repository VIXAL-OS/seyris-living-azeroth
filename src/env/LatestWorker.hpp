// One background thread with a "latest job wins" mailbox: one job runs, at most one waits, a new
// job replaces the waiting one. Used by env/Fields for the shore pass (env/ShoreField).
//
// Rules, because this lives in a DLL inside the game:
//  - Only the thread that constructed it (the game's main thread, from an ordinary per-frame
//    callback) calls Submit/TryTake/Invalidate/Stop; others get std::logic_error.
//  - Construct and Stop it from ordinary callbacks, NEVER from DllMain, a static initialiser or a
//    static destructor (those run under the loader lock: starting or joining a thread there hangs).
//    Keep it on the heap, not in a static object, so process exit never runs its destructor.
//  - Stop cancels the waiting job and joins the one running (bounded: one calculation). There is no
//    detached thread.
//  - TryTake never waits for a calculation; the mailbox is guarded by a mutex held only for moves.
//  - The calculation runs without the lock and must only use the input it was handed (which it
//    may take apart: the job is done with it).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace wxl_livingazeroth::fields::shore
{
    template <class Input, class Output> class LatestWorker
    {
    public:
        struct Completion
        {
            uint64_t              id = 0, epoch = 0;
            std::optional<Output> value;
            std::exception_ptr    error;
        };
        struct Stats { uint64_t submitted = 0, replaced = 0, discarded = 0, completed = 0; };

        /// Starts the thread. Throws (std::system_error, std::invalid_argument) if it can't.
        explicit LatestWorker(std::function<Output(Input&)> compute)
            : owner_(std::this_thread::get_id()), compute_(std::move(compute))
        {
            if (!compute_) throw std::invalid_argument("LatestWorker needs a calculation");
            thread_ = std::thread([this] { Run(); });
        }
        /// Same as Stop, without the owner check (a destructor must not throw).
        ~LatestWorker() { Shutdown(); }
        LatestWorker(const LatestWorker&) = delete;
        LatestWorker& operator=(const LatestWorker&) = delete;

        /// Queues a job, replacing one still waiting. Returns its id.
        uint64_t Submit(Input input)
        {
            CheckOwner();
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) throw std::logic_error("LatestWorker has stopped");
            const uint64_t id = ++latest_;
            if (pending_) ++stats_.replaced;
            pending_ = Job{ id, epoch_, std::move(input) };
            ready_.reset();
            ++stats_.submitted;
            wake_.notify_one();
            return id;
        }

        /// The latest job's completion, if it's in. Never waits for the calculation.
        std::optional<Completion> TryTake()
        {
            CheckOwner();
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
            if (!lock.owns_lock() || !ready_) return std::nullopt;
            std::optional<Completion> value = std::move(ready_);
            ready_.reset();
            return value;
        }

        /// Everything queued or running belongs to a world that's gone: drop it.
        void Invalidate()
        {
            CheckOwner();
            std::lock_guard<std::mutex> lock(mutex_);
            ++epoch_;
            pending_.reset();
            ready_.reset();
        }

        Stats GetStats()
        {
            CheckOwner();
            std::lock_guard<std::mutex> lock(mutex_);
            return stats_;
        }

        /// Cancels the waiting job, joins the running one. Idempotent.
        void Stop()
        {
            CheckOwner();
            Shutdown();
        }

        bool Joinable() const { return thread_.joinable(); }

    private:
        struct Job { uint64_t id, epoch; Input input; };

        void CheckOwner() const
        {
            if (std::this_thread::get_id() != owner_) throw std::logic_error("LatestWorker belongs to the thread that created it");
        }

        void Shutdown() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopped_ = true;
                pending_.reset();
                ready_.reset();
            }
            wake_.notify_one();
            if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
        }

        void Run()
        {
            for (;;)
            {
                std::optional<Job> job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait(lock, [this] { return stopped_ || pending_.has_value(); });
                    if (stopped_) return;
                    job = std::move(pending_);
                    pending_.reset();
                }
                Completion result;
                result.id = job->id;
                result.epoch = job->epoch;
                try { result.value = compute_(job->input); }
                catch (...) { result.error = std::current_exception(); }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    // Only the newest job of the current epoch is published.
                    if (stopped_ || result.epoch != epoch_ || result.id != latest_) ++stats_.discarded;
                    else { ready_ = std::move(result); ++stats_.completed; }
                }
            }
        }

        const std::thread::id                 owner_;
        std::function<Output(Input&)>        compute_;
        std::mutex                            mutex_;
        std::condition_variable               wake_;
        std::optional<Job>                    pending_;
        std::optional<Completion>             ready_;
        bool                                  stopped_ = false;
        uint64_t                              latest_ = 0, epoch_ = 0;
        Stats                                 stats_;
        std::thread                           thread_; // last: everything Run touches exists before it starts
    };
}
