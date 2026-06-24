#pragma once

#include <string>
#include <string_view>

#include <tutorflow/identity_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>

namespace tutorflow::gateway {

class GrpcIdentityClient final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "gateway-identity-grpc-client";

  GrpcIdentityClient(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  userver::formats::json::Value Register(
      const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value Login(
      const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value ValidateToken(
      std::string_view token,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value ChangePassword(
      const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value GetUser(
      std::string_view user_id,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value GetStudent(
      std::string_view student_id,
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value ListStudents(
      const tutorflow::clients::GrpcCallContext& call_context) const;
  userver::formats::json::Value CreateStudent(
      const userver::formats::json::Value& body,
      const tutorflow::clients::GrpcCallContext& call_context) const;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::identity::v1::IdentityServiceClient client_;
  tutorflow::clients::GrpcClientOptions options_;
};

}  // namespace tutorflow::gateway
