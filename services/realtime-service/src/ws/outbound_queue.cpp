#include "ws/outbound_queue.hpp"

#include <utility>

namespace tutorflow::realtime {

OutboundQueue::OutboundQueue()
    : queue_(Queue::Create()),
      producer_(queue_->GetMultiProducer()),
      consumer_(queue_->GetConsumer()),
      signal_(userver::engine::SingleConsumerEvent::NoAutoReset{}) {}

bool OutboundQueue::Push(std::string message) {
  if (closed_.load(std::memory_order_acquire)) return false;
  if (!producer_.PushNoblock(std::move(message))) return false;
  signal_.Send();
  return true;
}

bool OutboundQueue::TryPop(std::string& message) {
  return consumer_.PopNoblock(message);
}

void OutboundQueue::ResetSignal() noexcept { signal_.Reset(); }

userver::engine::SingleConsumerEvent& OutboundQueue::Signal() noexcept {
  return signal_;
}

void OutboundQueue::Close() noexcept {
  if (closed_.exchange(true, std::memory_order_acq_rel)) return;
  std::move(consumer_).Reset();
  signal_.Send();
}

}  // namespace tutorflow::realtime
