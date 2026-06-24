#include <tutorflow/clients/identity_grpc_client.hpp>

#include <chrono>

#include <userver/components/component_config.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace tutorflow::clients {
namespace {
namespace proto = tutorflow::identity::v1;

AccessCheckResult FromProto(
    const proto::CheckTeacherStudentAccessResponse& response) {
  AccessCheckResult result;
  result.allowed = response.allowed();
  if (response.has_status()) {
    result.status = response.status();
  }
  if (response.has_hourly_rate()) {
    result.hourly_rate = response.hourly_rate();
  }
  return result;
}

}  // namespace

GrpcIdentityClient::GrpcIdentityClient(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::IdentityServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout = std::chrono::milliseconds{
              config["timeout-ms"].As<int>(5000)},
      } {}

AccessCheckResult GrpcIdentityClient::CheckAccess(
    std::string_view teacher_id, std::string_view student_id) const {
  proto::CheckTeacherStudentAccessRequest request;
  request.set_teacher_id(std::string{teacher_id});
  request.set_student_id(std::string{student_id});

  try {
    return FromProto(client_.CheckTeacherStudentAccess(
        request,
        MakeGrpcCallOptions({}, options_, GrpcOperationKind::kIdempotent)));
  } catch (const userver::ugrpc::client::ErrorWithStatus& e) {
    throw MapGrpcStatusToServiceError(e.GetStatus());
  } catch (const userver::ugrpc::client::BaseError& e) {
    throw tutorflow::common::ServiceError::Internal(e.what());
  }
}

userver::yaml_config::Schema GrpcIdentityClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: identity-service gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: identity-service gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

}  // namespace tutorflow::clients
