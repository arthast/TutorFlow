#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <userver/utils/statistics/entry.hpp>
#include <userver/utils/statistics/rate_counter.hpp>

namespace userver::utils::statistics {
class Storage;
}

namespace tutorflow::events {

class OutboxStatistics final {
public:
  OutboxStatistics(userver::utils::statistics::Storage& storage,
                   std::string producer, std::string task_name);

  void Record(std::chrono::milliseconds duration,
              std::uint64_t published) noexcept;

private:
  std::string producer_;
  std::string task_name_;
  std::atomic<std::uint64_t> tick_duration_ms_{0};
  std::atomic<std::uint64_t> published_per_tick_{0};
  userver::utils::statistics::RateCounter published_total_;
  userver::utils::statistics::Entry registration_;
};

class OutboxTickScope final {
public:
  explicit OutboxTickScope(OutboxStatistics& statistics) noexcept;
  ~OutboxTickScope() noexcept;

  OutboxTickScope(const OutboxTickScope&) = delete;
  OutboxTickScope& operator=(const OutboxTickScope&) = delete;

  void EventPublished() noexcept;

private:
  OutboxStatistics& statistics_;
  std::chrono::steady_clock::time_point started_at_;
  std::uint64_t published_{0};
};

}  // namespace tutorflow::events
