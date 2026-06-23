#include "clients/identity_grpc_client.hpp"

#include <chrono>
#include <string>
#include <utility>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::identity::v1;
namespace common_proto = tutorflow::common::v1;

template <typename Func>
auto CallIdentity(Func&& func) {
  try {
    return std::forward<Func>(func)();
  } catch (const userver::ugrpc::client::ErrorWithStatus& e) {
    throw tutorflow::clients::MapGrpcStatusToServiceError(e.GetStatus());
  } catch (const userver::ugrpc::client::BaseError& e) {
    throw tutorflow::common::ServiceError::Internal(e.what());
  }
}

userver::ugrpc::client::CallOptions IdempotentOptions(
    const tutorflow::clients::GrpcCallContext& context,
    const tutorflow::clients::GrpcClientOptions& options) {
  return tutorflow::clients::MakeGrpcCallOptions(
      context, options, tutorflow::clients::GrpcOperationKind::kIdempotent);
}

userver::ugrpc::client::CallOptions NonIdempotentOptions(
    const tutorflow::clients::GrpcCallContext& context,
    const tutorflow::clients::GrpcClientOptions& options) {
  return tutorflow::clients::MakeGrpcCallOptions(
      context, options, tutorflow::clients::GrpcOperationKind::kNonIdempotent);
}

void FillUser(common_proto::UserContext* user,
              const tutorflow::clients::GrpcCallContext& context) {
  user->set_user_id(context.user_id);
  user->set_role(context.roles);
}

json::Value ToJson(const proto::TokenResponse& token) {
  json::ValueBuilder body;
  body["access_token"] = token.access_token();
  body["token_type"] = token.token_type();
  body["expires_in"] = token.expires_in();
  body["user_id"] = token.user_id();
  json::ValueBuilder roles(common_formats::Type::kArray);
  for (const auto& role : token.roles()) {
    roles.PushBack(role);
  }
  body["roles"] = roles.ExtractValue();
  return body.ExtractValue();
}

json::Value ToJson(const proto::TokenClaims& claims) {
  json::ValueBuilder body;
  body["sub"] = claims.sub();
  body["exp"] = claims.exp();
  json::ValueBuilder roles(common_formats::Type::kArray);
  for (const auto& role : claims.roles()) {
    roles.PushBack(role);
  }
  body["roles"] = roles.ExtractValue();
  return body.ExtractValue();
}

json::Value ToJson(const proto::User& user) {
  json::ValueBuilder body;
  body["id"] = user.id();
  body["email"] = user.email();
  body["role"] = user.role();
  body["display_name"] = user.display_name();
  body["created_at"] = user.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::StudentLink& student) {
  json::ValueBuilder body;
  body["id"] = student.id();
  body["teacher_id"] = student.teacher_id();
  body["student_id"] = student.student_id();
  body["display_name"] = student.display_name();
  body["subject"] = student.has_subject()
                        ? json::ValueBuilder(student.subject()).ExtractValue()
                        : json::ValueBuilder(nullptr).ExtractValue();
  body["goal"] = student.has_goal()
                     ? json::ValueBuilder(student.goal()).ExtractValue()
                     : json::ValueBuilder(nullptr).ExtractValue();
  body["hourly_rate"] = student.has_hourly_rate()
                            ? json::ValueBuilder(student.hourly_rate()).ExtractValue()
                            : json::ValueBuilder(nullptr).ExtractValue();
  body["status"] = student.status();
  body["created_at"] = student.created_at();
  return body.ExtractValue();
}

json::Value ToJsonArray(const proto::ListStudentsResponse& response) {
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto& student : response.students()) {
    array.PushBack(ToJson(student));
  }
  return array.ExtractValue();
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

json::Value GrpcIdentityClient::Register(
    const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::RegisterRequest request;
  request.set_email(tutorflow::common::RequireString(body, "email"));
  request.set_password(tutorflow::common::RequireString(body, "password"));
  request.set_role(tutorflow::common::RequireString(body, "role"));
  request.set_display_name(
      tutorflow::common::RequireString(body, "display_name"));
  if (const auto timezone = tutorflow::common::OptionalString(body, "timezone")) {
    request.set_timezone(*timezone);
  }
  return ToJson(CallIdentity([&] {
    return client_.Register(request, NonIdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::Login(
    const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::LoginRequest request;
  request.set_email(tutorflow::common::RequireString(body, "email"));
  request.set_password(tutorflow::common::RequireString(body, "password"));
  return ToJson(CallIdentity([&] {
    return client_.Login(request, NonIdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::ValidateToken(
    std::string_view token,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ValidateTokenRequest request;
  request.set_token(std::string{token});
  return ToJson(CallIdentity([&] {
    return client_.ValidateToken(request,
                                 IdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::ChangePassword(
    const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ChangePasswordRequest request;
  FillUser(request.mutable_user(), call_context);
  request.set_current_password(
      tutorflow::common::RequireString(body, "current_password"));
  request.set_new_password(tutorflow::common::RequireString(body, "new_password"));
  CallIdentity([&] {
    return client_.ChangePassword(request,
                                  NonIdempotentOptions(call_context, options_));
  });
  json::ValueBuilder response;
  response["status"] = "ok";
  return response.ExtractValue();
}

json::Value GrpcIdentityClient::GetUser(
    std::string_view user_id,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::GetUserRequest request;
  request.set_user_id(std::string{user_id});
  return ToJson(CallIdentity([&] {
    return client_.GetUser(request, IdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::GetStudent(
    std::string_view student_id,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::GetStudentRequest request;
  FillUser(request.mutable_user(), call_context);
  request.set_student_id(std::string{student_id});
  return ToJson(CallIdentity([&] {
    return client_.GetStudent(request,
                              IdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::ListStudents(
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::ListStudentsRequest request;
  FillUser(request.mutable_user(), call_context);
  request.set_teacher_id(call_context.user_id);
  return ToJsonArray(CallIdentity([&] {
    return client_.ListStudents(request,
                                IdempotentOptions(call_context, options_));
  }));
}

json::Value GrpcIdentityClient::CreateStudent(
    const json::Value& body,
    const tutorflow::clients::GrpcCallContext& call_context) const {
  proto::CreateStudentRequest request;
  FillUser(request.mutable_user(), call_context);
  request.set_email(tutorflow::common::RequireString(body, "email"));
  request.set_password(tutorflow::common::RequireString(body, "password"));
  request.set_display_name(
      tutorflow::common::RequireString(body, "display_name"));
  if (const auto subject = tutorflow::common::OptionalString(body, "subject")) {
    request.set_subject(*subject);
  }
  if (const auto goal = tutorflow::common::OptionalString(body, "goal")) {
    request.set_goal(*goal);
  }
  if (const auto hourly_rate =
          tutorflow::common::OptionalDouble(body, "hourly_rate")) {
    request.set_hourly_rate(*hourly_rate);
  }
  return ToJson(CallIdentity([&] {
    return client_.CreateStudent(request,
                                 NonIdempotentOptions(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcIdentityClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway identity gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: identity gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

}  // namespace tutorflow::gateway
