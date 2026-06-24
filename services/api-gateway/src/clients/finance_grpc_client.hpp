#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <tutorflow/finance_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcFinanceClient final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "gateway-finance-grpc-client";

  GrpcFinanceClient(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  userver::formats::json::Value
  GetBalance(std::string_view student_id,
             const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value ListTransactions(
      std::string_view student_id,
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  ListReceipts(const std::optional<std::string> &status,
               const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  CreateReceipt(const userver::formats::json::Value &body,
                const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  ConfirmReceipt(std::string_view receipt_id,
                 const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  RejectReceipt(std::string_view receipt_id,
                const userver::formats::json::Value &body,
                const tutorflow::clients::GrpcCallContext &call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::finance::v1::FinanceServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

} // namespace tutorflow::gateway
