#include <tutorflow/clients/grpc_client_base.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/error_codes.hpp>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/status_codes.hpp>

namespace tutorflow::clients {
namespace {

namespace http = userver::server::http;
namespace json = userver::formats::json;

constexpr std::string_view kMetadataRequestId = "x-request-id";
constexpr std::string_view kMetadataTraceId = "x-trace-id";
constexpr std::string_view kMetadataUserId = "x-user-id";
constexpr std::string_view kMetadataUserRoles = "x-user-roles";
constexpr std::string_view kEmailTaken = "email_taken";

http::HttpStatus ToHttpStatus(grpc::StatusCode code) {
  switch (code) {
    case grpc::StatusCode::OK:
      return http::HttpStatus::kOk;
    case grpc::StatusCode::INVALID_ARGUMENT:
      return http::HttpStatus::kBadRequest;
    case grpc::StatusCode::UNAUTHENTICATED:
      return http::HttpStatus::kUnauthorized;
    case grpc::StatusCode::PERMISSION_DENIED:
      return http::HttpStatus::kForbidden;
    case grpc::StatusCode::NOT_FOUND:
      return http::HttpStatus::kNotFound;
    case grpc::StatusCode::ALREADY_EXISTS:
      return http::HttpStatus::kConflict;
    case grpc::StatusCode::FAILED_PRECONDITION:
      return http::HttpStatus::kUnprocessableEntity;
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return http::HttpStatus::kGatewayTimeout;
    case grpc::StatusCode::UNAVAILABLE:
      return http::HttpStatus::kServiceUnavailable;
    default:
      return http::HttpStatus::kInternalServerError;
  }
}

std::string ToErrorCode(const grpc::Status& status) {
  switch (status.error_code()) {
    case grpc::StatusCode::INVALID_ARGUMENT:
      return std::string{tutorflow::common::error_code::kValidation};
    case grpc::StatusCode::UNAUTHENTICATED:
      return std::string{tutorflow::common::error_code::kUnauthorized};
    case grpc::StatusCode::PERMISSION_DENIED:
      return std::string{tutorflow::common::error_code::kForbidden};
    case grpc::StatusCode::NOT_FOUND:
      return std::string{tutorflow::common::error_code::kNotFound};
    case grpc::StatusCode::ALREADY_EXISTS:
      if (status.error_details() == kEmailTaken) {
        return std::string{kEmailTaken};
      }
      return std::string{tutorflow::common::error_code::kConflict};
    case grpc::StatusCode::FAILED_PRECONDITION:
      return std::string{tutorflow::common::error_code::kBusinessRule};
    default:
      return std::string{tutorflow::common::error_code::kInternal};
  }
}

json::Value BuildDetails(const grpc::Status& status) {
  json::ValueBuilder details(userver::formats::common::Type::kObject);
  details["grpc_status"] = userver::ugrpc::ToString(status.error_code());
  if (!status.error_details().empty()) {
    details["grpc_details"] = status.error_details();
  }
  return details.ExtractValue();
}

std::string BuildMessage(const grpc::Status& status) {
  if (!status.error_message().empty()) {
    return status.error_message();
  }
  return std::string{"upstream grpc service error"};
}

}  // namespace

GrpcCallContext GrpcCallContext::FromCommon(
    const tutorflow::common::CallContext& ctx) {
  return {
      .user_id = ctx.user_id,
      .roles = ctx.roles,
      .request_id = ctx.request_id,
      .trace_id = {},
  };
}

userver::ugrpc::client::CallOptions MakeGrpcCallOptions(
    const GrpcCallContext& ctx, const GrpcClientOptions& options,
    GrpcOperationKind operation_kind) {
  userver::ugrpc::client::CallOptions call_options;
  call_options.SetTimeout(options.timeout);
  call_options.SetAttempts(operation_kind == GrpcOperationKind::kIdempotent
                               ? options.idempotent_attempts
                               : 1);

  if (!ctx.user_id.empty()) {
    call_options.AddMetadata(kMetadataUserId, ctx.user_id);
  }
  if (!ctx.roles.empty()) {
    call_options.AddMetadata(kMetadataUserRoles, ctx.roles);
  }
  if (!ctx.request_id.empty()) {
    call_options.AddMetadata(kMetadataRequestId, ctx.request_id);
  }
  if (!ctx.trace_id.empty()) {
    call_options.AddMetadata(kMetadataTraceId, ctx.trace_id);
  }
  return call_options;
}

tutorflow::common::ServiceError MapGrpcStatusToServiceError(
    const grpc::Status& status) {
  return tutorflow::common::ServiceError(ToHttpStatus(status.error_code()),
                                         ToErrorCode(status),
                                         BuildMessage(status),
                                         BuildDetails(status));
}

}  // namespace tutorflow::clients
