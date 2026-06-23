#include "handlers/health_handler.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>

#include "cors.hpp"

namespace tutorflow::gateway {

HealthHandler::HealthHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      settings_(context.FindComponent<GatewaySettings>()) {}

std::string HealthHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& /*context*/) const {
  if (IsOptionsRequest(request)) {
    return MakePreflightResponse(request, settings_);
  }

  ApplyCorsHeaders(request, settings_);
  request.GetHttpResponse().SetContentType(
      userver::http::content_type::kApplicationJson);

  userver::formats::json::ValueBuilder body(
      userver::formats::common::Type::kObject);
  body["status"] = "ok";
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace tutorflow::gateway
