#include <tutorflow/events/outbox_statistics.hpp>

#include <utility>

#include <userver/logging/log.hpp>
#include <userver/utils/statistics/labels.hpp>
#include <userver/utils/statistics/storage.hpp>
#include <userver/utils/statistics/writer.hpp>

namespace tutorflow::events {

OutboxStatistics::OutboxStatistics(
    userver::utils::statistics::Storage& storage, std::string producer,
    std::string task_name)
    : producer_(std::move(producer)),
      task_name_(std::move(task_name)),
      registration_(storage.RegisterWriter(
          "tutorflow.outbox",
          [this](userver::utils::statistics::Writer& writer) {
            writer["tick_duration_ms"] = tick_duration_ms_;
            writer["published_per_tick"] = published_per_tick_;
            writer["published_total"] = published_total_;
          },
          {{"producer", producer_}, {"task", task_name_}})) {}

void OutboxStatistics::Record(std::chrono::milliseconds duration,
                              std::uint64_t published) noexcept {
  tick_duration_ms_.store(static_cast<std::uint64_t>(duration.count()),
                          std::memory_order_relaxed);
  published_per_tick_.store(published, std::memory_order_relaxed);
  published_total_.Add(userver::utils::statistics::Rate{published});

  if (duration > std::chrono::seconds{1}) {
    LOG_WARNING() << "[outbox] tick took too long producer=" << producer_
                  << " task=" << task_name_
                  << " duration_ms=" << duration.count()
                  << " published=" << published;
  }
}

OutboxTickScope::OutboxTickScope(OutboxStatistics& statistics) noexcept
    : statistics_(statistics), started_at_(std::chrono::steady_clock::now()) {}

OutboxTickScope::~OutboxTickScope() noexcept {
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at_);
  statistics_.Record(duration, published_);
}

void OutboxTickScope::EventPublished() noexcept { ++published_; }

}  // namespace tutorflow::events
