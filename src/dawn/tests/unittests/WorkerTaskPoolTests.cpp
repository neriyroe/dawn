// Copyright 2026 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <memory>
#include <atomic>
#include <future>

#include "dawn/platform/DawnPlatform.h"
#include "gtest/gtest.h"
#include "src/dawn/tests/MockCallback.h"

namespace dawn {
namespace {

using testing::NotNull;

using MockTaskCallback = testing::MockCallback<platform::PostWorkerTaskCallback>;
using MockJobCallback = testing::MockCallback<platform::PostWorkerJobCallback>;

class WorkerTaskPoolTests : public testing::Test {
  protected:
    WorkerTaskPoolTests() : pool(platform.CreateWorkerTaskPool()) {}

    platform::Platform platform;
    std::unique_ptr<platform::WorkerTaskPool> pool;
};

// Verifies that clients can create Dawn's default worker task pool with a custom thread count.
TEST(WorkerTaskPoolFactoryTests, CreateDawnDefault) {
    EXPECT_THAT(platform::WorkerTaskPool::CreateDawnDefault(1), NotNull());
}

TEST(WorkerTaskPoolFactoryTests, ActiveAndQueuedTasksDrainOnDestruction) {
    auto pool = platform::WorkerTaskPool::CreateDawnDefault(1);
    const auto& observed = *pool;
    EXPECT_TRUE(observed.IsIdle());
    struct Work {
        std::promise<void> entered;
        std::promise<void> release;
        std::atomic<int> completed = 0;
    } work;
    auto first = pool->PostWorkerTask([](void* data) {
        auto& work = *static_cast<Work*>(data);
        work.entered.set_value();
        work.release.get_future().wait();
        work.completed++;
    }, &work);
    work.entered.get_future().wait();
    auto second = pool->PostWorkerTask([](void* data) {
        static_cast<Work*>(data)->completed++;
    }, &work);
    EXPECT_FALSE(observed.IsIdle());
    work.release.set_value();
    pool.reset();
    EXPECT_EQ(work.completed, 2);
    EXPECT_TRUE(first->IsComplete());
    EXPECT_TRUE(second->IsComplete());
}

TEST(WorkerTaskPoolFactoryTests, SleepingWorkersWakeForNewTasksAndShutdown) {
    auto pool = platform::WorkerTaskPool::CreateDawnDefault(4);
    std::atomic<int> completed = 0;
    for (int round = 0; round < 32; round++) {
        auto event = pool->PostWorkerTask([](void* data) {
            (*static_cast<std::atomic<int>*>(data))++;
        }, &completed);
        event->Wait();
    }
    pool.reset();
    EXPECT_EQ(completed, 32);
}

TEST(WorkerTaskPoolFactoryTests, ShutdownCancelsJobsBeforeDrainingTasks) {
    auto pool = platform::WorkerTaskPool::CreateDawnDefault(2);
    struct Work {
        platform::WorkerTaskPool* pool;
        std::atomic<int> completed = 0;
        std::promise<void> entered;
        bool posted = false;
    } work{pool.get()};
    auto job = pool->PostWorkerJob([](void* data) {
        auto& work = *static_cast<Work*>(data);
        work.pool->PostWorkerTask([](void* data) {
            static_cast<Work*>(data)->completed++;
        }, &work);
        if (!work.posted) { work.posted = true; work.entered.set_value(); }
        return platform::JobStatus::Continue;
    }, &work);
    work.entered.get_future().wait();
    pool.reset();
    EXPECT_GT(work.completed, 0);
    job->Join();
}

// Verifies that a task does work on another thread and we can wait on it.
TEST_F(WorkerTaskPoolTests, PostTask) {
    int result = 0;

    MockTaskCallback cb;
    EXPECT_CALL(cb, Call).WillOnce([&result]() { result += 1; });

    auto event = pool->PostWorkerTask(cb.Callback(), cb.MakeUserdata(nullptr));
    ASSERT_THAT(event, NotNull());
    event->Wait();

    EXPECT_TRUE(event->IsComplete());
    EXPECT_EQ(result, 1);
}

// Verifies that a job does work and can complete.
TEST_F(WorkerTaskPoolTests, PostJob) {
    int result = 0;

    MockJobCallback cb;
    EXPECT_CALL(cb, Call)
        .WillOnce([&result]() {
            result += 1;
            return platform::JobStatus::Continue;
        })
        .WillOnce([&result]() {
            result += 1;
            return platform::JobStatus::Continue;
        })
        .WillOnce([&result]() {
            result += 1;
            return platform::JobStatus::Completed;
        });

    auto handle = pool->PostWorkerJob(cb.Callback(), cb.MakeUserdata(nullptr));
    ASSERT_THAT(handle, NotNull());
    handle->Join();

    EXPECT_EQ(result, 3);
}

// Verifies that a job that completes can be joined.
TEST_F(WorkerTaskPoolTests, PostJobComplete) {
    MockJobCallback cb;
    EXPECT_CALL(cb, Call).WillOnce([]() { return platform::JobStatus::Completed; });

    auto handle = pool->PostWorkerJob(cb.Callback(), cb.MakeUserdata(nullptr));
    ASSERT_THAT(handle, NotNull());
    handle->Join();
}

// Verifies that a non-terminating job can be cancelled and joined.
TEST_F(WorkerTaskPoolTests, PostJobCancel) {
    MockJobCallback cb;
    EXPECT_CALL(cb, Call).WillRepeatedly([]() { return platform::JobStatus::Continue; });

    auto handle = pool->PostWorkerJob(cb.Callback(), cb.MakeUserdata(nullptr));
    ASSERT_THAT(handle, NotNull());
    handle->Cancel();
    handle->Join();
}

}  // anonymous namespace
}  // namespace dawn
