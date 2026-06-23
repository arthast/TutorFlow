#pragma once

#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <userver/formats/json/value.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::common {

std::string JsonResponse(
    const userver::server::http::HttpRequest& request,
    userver::formats::json::Value body,
    userver::server::http::HttpStatus status =
        userver::server::http::HttpStatus::kOk);

std::string ErrorResponse(
    const userver::server::http::HttpRequest& request,
    const ServiceError& error);

template <typename Func>
std::string HandleEnvelope(
    const userver::server::http::HttpRequest& request, Func&& func) {
  try {
    return std::forward<Func>(func)();
  } catch (const ServiceError& error) {
    return ErrorResponse(request, error);
  } catch (const std::exception& error) {
    return ErrorResponse(request, ServiceError::Internal(error.what()));
  }
}

userver::formats::json::Value ParseJsonBody(
    const userver::server::http::HttpRequest& request,
    bool allow_empty = false);

std::string RequireString(const userver::formats::json::Value& body,
                          std::string_view field);

std::optional<std::string> OptionalString(
    const userver::formats::json::Value& body, std::string_view field);

double RequireDouble(const userver::formats::json::Value& body,
                     std::string_view field);

std::optional<double> OptionalDouble(
    const userver::formats::json::Value& body, std::string_view field);

}  // namespace tutorflow::common
