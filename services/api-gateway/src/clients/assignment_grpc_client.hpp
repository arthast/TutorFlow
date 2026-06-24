#pragma once

#include <string>
#include <string_view>

#include <tutorflow/assignment_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcAssignmentClient final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "gateway-assignment-grpc-client";

  GrpcAssignmentClient(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  userver::formats::json::Value
  ListAssignments(const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value CreateAssignment(
      const userver::formats::json::Value &body,
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  GetAssignment(std::string_view assignment_id,
                const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value SubmitAssignment(
      std::string_view assignment_id, const userver::formats::json::Value &body,
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value ReviewAssignment(
      std::string_view assignment_id, const userver::formats::json::Value &body,
      const tutorflow::clients::GrpcCallContext &call_context) const;
  userver::formats::json::Value
  AddComment(std::string_view assignment_id,
             const userver::formats::json::Value &body,
             const tutorflow::clients::GrpcCallContext &call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::assignment::v1::AssignmentServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

} // namespace tutorflow::gateway
