#include <tutorflow/events/outbox_statistics.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <userver/utils/statistics/labels.hpp>
#include <userver/utils/statistics/storage.hpp>
#include <userver/utils/statistics/testing.hpp>
#include <userver/utest/utest.hpp>

namespace tutorflow::events {
namespace {

namespace statistics = userver::utils::statistics;

UTEST(OutboxStatistics, DistinguishesChatShardsByTaskLabel) {
  statistics::Storage storage;
  OutboxStatistics shard0{storage, "chat-service",
                          "chat-outbox-publisher-shard0"};
  OutboxStatistics shard1{storage, "chat-service",
                          "chat-outbox-publisher-shard1"};

  shard0.Record(std::chrono::milliseconds{25}, 2);
  shard1.Record(std::chrono::milliseconds{40}, 5);

  const statistics::Snapshot snapshot{storage, "tutorflow.outbox"};
  const std::vector<statistics::Label> shard0_labels{
      {"producer", "chat-service"},
      {"task", "chat-outbox-publisher-shard0"},
  };
  const std::vector<statistics::Label> shard1_labels{
      {"producer", "chat-service"},
      {"task", "chat-outbox-publisher-shard1"},
  };

  EXPECT_EQ(snapshot.SingleMetric("tick_duration_ms", shard0_labels).AsInt(),
            25);
  EXPECT_EQ(
      snapshot.SingleMetric("published_per_tick", shard0_labels).AsInt(), 2);
  EXPECT_EQ(snapshot.SingleMetric("tick_duration_ms", shard1_labels).AsInt(),
            40);
  EXPECT_EQ(
      snapshot.SingleMetric("published_per_tick", shard1_labels).AsInt(), 5);
}

UTEST(OutboxStatistics, AccumulatesPublishedTotalAcrossTicks) {
  statistics::Storage storage;
  OutboxStatistics outbox{storage, "lesson-service",
                          "lesson-outbox-publisher"};

  outbox.Record(std::chrono::milliseconds{10}, 2);
  outbox.Record(std::chrono::milliseconds{15}, 3);

  const statistics::Snapshot snapshot{storage, "tutorflow.outbox"};
  EXPECT_EQ(snapshot.SingleMetric("published_total").AsRate(),
            statistics::Rate{5});
}

UTEST(OutboxTickScope, RecordsZeroForTickWithoutPublishedEvents) {
  statistics::Storage storage;
  OutboxStatistics outbox{storage, "lesson-service",
                          "lesson-outbox-publisher"};

  { OutboxTickScope tick{outbox}; }

  const statistics::Snapshot snapshot{storage, "tutorflow.outbox"};
  EXPECT_EQ(snapshot.SingleMetric("published_per_tick").AsInt(), 0);
}

UTEST(OutboxTickScope, RecordsMetricsWithoutMaskingOriginalException) {
  statistics::Storage storage;
  OutboxStatistics outbox{storage, "lesson-service",
                          "lesson-outbox-publisher"};

  try {
    OutboxTickScope tick{outbox};
    tick.EventPublished();
    throw std::runtime_error{"original publish failure"};
  } catch (const std::runtime_error& exception) {
    EXPECT_EQ(std::string{exception.what()}, "original publish failure");
  }

  const statistics::Snapshot snapshot{storage, "tutorflow.outbox"};
  EXPECT_EQ(snapshot.SingleMetric("published_per_tick").AsInt(), 1);
}

}  // namespace
}  // namespace tutorflow::events
