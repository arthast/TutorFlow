#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <userver/concurrent/mpsc_queue.hpp>
#include <userver/engine/single_consumer_event.hpp>

namespace tutorflow::realtime {

class OutboundQueue final {
public:
  OutboundQueue();

  bool Push(std::string message);
  bool TryPop(std::string& message);
  void ResetSignal() noexcept;
  userver::engine::SingleConsumerEvent& Signal() noexcept;
  void Close() noexcept;

private:
  using Queue = userver::concurrent::MpscQueue<std::string>;

  std::shared_ptr<Queue> queue_;
  Queue::MultiProducer producer_;
  Queue::Consumer consumer_;
  userver::engine::SingleConsumerEvent signal_;
  std::atomic<bool> closed_{false};
};

}  // namespace tutorflow::realtime
