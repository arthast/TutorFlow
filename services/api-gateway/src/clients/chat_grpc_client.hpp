#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <tutorflow/chat_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcChatClient final
    : public userver::components::LoggableComponentBase {
 public:
  static constexpr std::string_view kName = "gateway-chat-grpc-client";

  GrpcChatClient(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

  userver::formats::json::Value CreateDialog(
      const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value ListDialogs(
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value ListMessages(
      std::string_view dialog_id, const std::optional<std::string>& before,
      const std::optional<std::string>& limit,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value SendMessage(
      std::string_view dialog_id, const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value MarkRead(
      std::string_view dialog_id, const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

 private:
  tutorflow::chat::v1::ChatServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

}  // namespace tutorflow::gateway
