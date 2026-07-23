#include "ws/outbound_queue.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include <userver/engine/async.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/engine/wait_any.hpp>
#include <userver/utest/utest.hpp>

namespace tutorflow::realtime {
namespace {

UTEST(OutboundQueue, PreservesSequentialOrder) {
  OutboundQueue queue;
  ASSERT_TRUE(queue.Push("one"));
  ASSERT_TRUE(queue.Push("two"));

  std::string actual;
  ASSERT_TRUE(queue.TryPop(actual));
  EXPECT_EQ(actual, "one");
  ASSERT_TRUE(queue.TryPop(actual));
  EXPECT_EQ(actual, "two");
  EXPECT_FALSE(queue.TryPop(actual));
}

UTEST_MT(OutboundQueue, AcceptsMultipleConcurrentProducersWithoutLoss, 4) {
  constexpr int kProducerCount = 4;
  constexpr int kMessagesPerProducer = 100;

  OutboundQueue queue;
  std::vector<userver::engine::TaskWithResult<void>> producers;
  producers.reserve(kProducerCount);
  for (int producer = 0; producer < kProducerCount; ++producer) {
    producers.push_back(userver::engine::AsyncNoTracing(
        [&queue, producer] {
          for (int index = 0; index < kMessagesPerProducer; ++index) {
            if (!queue.Push(std::to_string(producer) + ":" +
                            std::to_string(index))) {
              throw std::runtime_error{"unexpected closed outbound queue"};
            }
          }
        }));
  }
  for (auto& producer : producers) producer.Get();

  std::unordered_set<std::string> actual;
  std::string message;
  while (queue.TryPop(message)) actual.insert(message);

  ASSERT_EQ(actual.size(), kProducerCount * kMessagesPerProducer);
  for (int producer = 0; producer < kProducerCount; ++producer) {
    for (int index = 0; index < kMessagesPerProducer; ++index) {
      EXPECT_TRUE(actual.contains(std::to_string(producer) + ":" +
                                  std::to_string(index)));
    }
  }
}

UTEST(OutboundQueue, WakesWaitingConsumer) {
  OutboundQueue queue;
  auto waiter = userver::engine::AsyncNoTracing([&queue] {
    auto wait = userver::engine::MakeWaitAny(queue.Signal());
    EXPECT_TRUE(wait.WaitFor(std::chrono::seconds{1}).has_value());
  });

  userver::engine::Yield();
  ASSERT_TRUE(queue.Push("wake"));
  waiter.Get();
}

UTEST(OutboundQueue, ResetThenRecheckKeepsQueuedMessage) {
  OutboundQueue queue;
  ASSERT_TRUE(queue.Push("during-transition"));

  queue.ResetSignal();
  std::string actual;
  ASSERT_TRUE(queue.TryPop(actual));
  EXPECT_EQ(actual, "during-transition");
}

UTEST(OutboundQueue, RejectsPushAfterClose) {
  OutboundQueue queue;
  queue.Close();

  EXPECT_FALSE(queue.Push("late"));
}

}  // namespace
}  // namespace tutorflow::realtime
