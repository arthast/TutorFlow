#include "cors.hpp"

#include <string>

#include <userver/server/http/http_status.hpp>

namespace tutorflow::gateway {
namespace {

constexpr auto kAllowMethods = "GET, POST, OPTIONS";
constexpr auto kAllowHeaders = "Authorization, Content-Type, X-Request-Id";
constexpr auto kMaxAgeSeconds = "600";

}  // namespace

void ApplyCorsHeaders(const userver::server::http::HttpRequest& request,
                      const GatewaySettings& settings) {
  auto& response = request.GetHttpResponse();
  response.SetHeader(std::string{"Access-Control-Allow-Origin"},
                     settings.CorsOrigin());
  response.SetHeader(std::string{"Access-Control-Allow-Methods"},
                     std::string{kAllowMethods});
  response.SetHeader(std::string{"Access-Control-Allow-Headers"},
                     std::string{kAllowHeaders});
  response.SetHeader(std::string{"Access-Control-Max-Age"},
                     std::string{kMaxAgeSeconds});
}

bool IsOptionsRequest(const userver::server::http::HttpRequest& request) {
  return request.GetMethodStr() == "OPTIONS";
}

std::string MakePreflightResponse(
    const userver::server::http::HttpRequest& request,
    const GatewaySettings& settings) {
  ApplyCorsHeaders(request, settings);
  request.GetHttpResponse().SetStatus(
      static_cast<userver::server::http::HttpStatus>(204));
  return {};
}

}  // namespace tutorflow::gateway
