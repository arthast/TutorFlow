#include <tutorflow/common/handler_helpers.hpp>

#include <exception>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::common {
namespace {
namespace common_formats = userver::formats::common;
namespace http = userver::server::http;
namespace json = userver::formats::json;
}  // namespace

std::string JsonResponse(const http::HttpRequest& request, json::Value body,
                         http::HttpStatus status) {
  request.GetHttpResponse().SetStatus(status);
  request.GetHttpResponse().SetHeader(
      std::string{"Content-Type"},
      std::string{"application/json; charset=utf-8"});
  return json::ToString(body);
}

std::string ErrorResponse(const http::HttpRequest& request,
                          const ServiceError& error) {
  return JsonResponse(request, MakeErrorBody(error), error.Status());
}

json::Value ParseJsonBody(const http::HttpRequest& request, bool allow_empty) {
  if (allow_empty && request.RequestBody().empty()) {
    return json::ValueBuilder(common_formats::Type::kObject).ExtractValue();
  }
  try {
    return json::FromString(request.RequestBody());
  } catch (const std::exception&) {
    throw ServiceError::Validation("request body must be valid JSON");
  }
}

std::string RequireString(const json::Value& body, std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    throw ServiceError::Validation("missing required field: " + key);
  }
  auto value = body[key].As<std::string>("");
  if (value.empty()) {
    throw ServiceError::Validation("field must not be empty: " + key);
  }
  return value;
}

std::optional<std::string> OptionalString(const json::Value& body,
                                          std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    return std::nullopt;
  }
  auto value = body[key].As<std::string>("");
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

double RequireDouble(const json::Value& body, std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    throw ServiceError::Validation("missing required field: " + key);
  }
  try {
    return body[key].As<double>();
  } catch (const std::exception&) {
    throw ServiceError::Validation("field must be a number: " + key);
  }
}

std::optional<double> OptionalDouble(const json::Value& body,
                                     std::string_view field) {
  const std::string key{field};
  if (!body.HasMember(key) || body[key].IsNull()) {
    return std::nullopt;
  }
  try {
    return body[key].As<double>();
  } catch (const std::exception&) {
    throw ServiceError::Validation("field must be a number: " + key);
  }
}

}  // namespace tutorflow::common
