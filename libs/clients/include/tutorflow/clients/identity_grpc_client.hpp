#pragma once

#include <string_view>

#include <tutorflow/identity_client.usrv.pb.hpp>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include <tutorflow/clients/grpc_client_base.hpp>
#include <tutorflow/clients/identity_client.hpp>

namespace tutorflow::clients {

class GrpcIdentityClient final
    : public userver::components::LoggableComponentBase,
      public IdentityClient {
public:
  static constexpr std::string_view kName = "identity-client";

  GrpcIdentityClient(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

  AccessCheckResult CheckAccess(std::string_view teacher_id,
                                std::string_view student_id) const override;

  static userver::yaml_config::Schema GetStaticConfigSchema();

private:
  tutorflow::identity::v1::IdentityServiceClient client_;
  GrpcClientOptions options_;
};

}  // namespace tutorflow::clients
