#include <tutorflow/common/errors.hpp>

#include <tutorflow/common/error_codes.hpp>

#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::common {
namespace {
namespace http = userver::server::http;
}  // namespace

ServiceError::ServiceError(http::HttpStatus status, std::string code,
                           std::string message,
                           userver::formats::json::Value details)
    : std::runtime_error(std::move(message)),
      status_(status),
      code_(std::move(code)),
      details_(std::move(details)) {}

ServiceError ServiceError::Validation(std::string message,
                                      userver::formats::json::Value details) {
  return ServiceError(http::HttpStatus::kBadRequest,
                      std::string{error_code::kValidation}, std::move(message),
                      std::move(details));
}

ServiceError ServiceError::Unauthorized(std::string message) {
  return ServiceError(http::HttpStatus::kUnauthorized,
                      std::string{error_code::kUnauthorized},
                      std::move(message));
}

ServiceError ServiceError::Forbidden(std::string message) {
  return ServiceError(http::HttpStatus::kForbidden,
                      std::string{error_code::kForbidden}, std::move(message));
}

ServiceError ServiceError::NotFound(std::string message) {
  return ServiceError(http::HttpStatus::kNotFound,
                      std::string{error_code::kNotFound}, std::move(message));
}

ServiceError ServiceError::Conflict(std::string message) {
  return ServiceError(http::HttpStatus::kConflict,
                      std::string{error_code::kConflict}, std::move(message));
}

ServiceError ServiceError::BusinessRule(std::string message,
                                        userver::formats::json::Value details) {
  return ServiceError(http::HttpStatus::kUnprocessableEntity,
                      std::string{error_code::kBusinessRule},
                      std::move(message), std::move(details));
}

ServiceError ServiceError::Internal(std::string message) {
  return ServiceError(http::HttpStatus::kInternalServerError,
                      std::string{error_code::kInternal}, std::move(message));
}

userver::formats::json::Value MakeErrorBody(
    std::string_view code, std::string_view message,
    userver::formats::json::Value details) {
  using userver::formats::json::ValueBuilder;
  namespace common = userver::formats::common;

  ValueBuilder error(common::Type::kObject);
  error["code"] = std::string{code};
  error["message"] = std::string{message};
  if (details.IsNull()) {
    error["details"] = ValueBuilder(common::Type::kObject).ExtractValue();
  } else {
    error["details"] = std::move(details);
  }

  ValueBuilder root(common::Type::kObject);
  root["error"] = error.ExtractValue();
  return root.ExtractValue();
}

userver::formats::json::Value MakeErrorBody(const ServiceError& error) {
  return MakeErrorBody(error.Code(), error.what(), error.Details());
}

}  // namespace tutorflow::common
