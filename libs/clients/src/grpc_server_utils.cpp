#include <tutorflow/clients/grpc_server_utils.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <userver/server/http/http_status.hpp>
#include <userver/ugrpc/server/metadata_utils.hpp>

namespace tutorflow::clients {
namespace {
namespace http = userver::server::http;

constexpr std::string_view kMetadataUserId = "x-user-id";
constexpr std::string_view kMetadataUserRoles = "x-user-roles";
constexpr std::string_view kEmailTaken = "email_taken";

std::string FirstMetadata(userver::ugrpc::server::CallContext& context,
                          std::string_view name) {
  for (const auto value :
       userver::ugrpc::server::GetRepeatedMetadata(context, name)) {
    return std::string{value};
  }
  return {};
}

std::vector<std::string> SplitCsv(std::string_view csv) {
  std::vector<std::string> values;
  std::size_t pos = 0;
  while (pos <= csv.size()) {
    const auto comma = csv.find(',', pos);
    const auto chunk = csv.substr(
        pos, comma == std::string_view::npos ? std::string_view::npos
                                             : comma - pos);
    if (!chunk.empty()) {
      values.emplace_back(chunk);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    pos = comma + 1;
  }
  return values;
}

grpc::StatusCode ToGrpcCode(http::HttpStatus status) {
  switch (status) {
    case http::HttpStatus::kBadRequest:
      return grpc::StatusCode::INVALID_ARGUMENT;
    case http::HttpStatus::kUnauthorized:
      return grpc::StatusCode::UNAUTHENTICATED;
    case http::HttpStatus::kForbidden:
      return grpc::StatusCode::PERMISSION_DENIED;
    case http::HttpStatus::kNotFound:
      return grpc::StatusCode::NOT_FOUND;
    case http::HttpStatus::kConflict:
      return grpc::StatusCode::ALREADY_EXISTS;
    case http::HttpStatus::kUnprocessableEntity:
      return grpc::StatusCode::FAILED_PRECONDITION;
    default:
      return grpc::StatusCode::INTERNAL;
  }
}

}  // namespace

tutorflow::common::AuthContext ResolveServerAuthContext(
    userver::ugrpc::server::CallContext& context,
    const tutorflow::common::v1::UserContext& user) {
  tutorflow::common::AuthContext auth{
      .user_id = FirstMetadata(context, kMetadataUserId),
      .roles = SplitCsv(FirstMetadata(context, kMetadataUserRoles)),
  };
  if (auth.user_id.empty()) {
    auth.user_id = user.user_id();
  }
  if (auth.roles.empty()) {
    auth.roles = SplitCsv(user.role());
  }
  return auth;
}

grpc::Status ServiceErrorToGrpcStatus(
    const tutorflow::common::ServiceError& error) {
  const auto code = ToGrpcCode(error.Status());
  const std::string details =
      error.Code() == kEmailTaken ? std::string{kEmailTaken} : std::string{};
  return grpc::Status{code, error.what(), details};
}

}  // namespace tutorflow::clients
