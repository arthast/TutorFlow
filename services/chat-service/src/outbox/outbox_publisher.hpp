#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include <tutorflow/events/outbox_publisher.hpp>

namespace tutorflow::chat {

class OutboxPublisher final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "chat-outbox-publisher";

  OutboxPublisher(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

  void OnAllComponentsLoaded() override;

private:
  std::vector<std::unique_ptr<tutorflow::events::PostgresOutboxPublisher>>
      publishers_;
};

}  // namespace tutorflow::chat
