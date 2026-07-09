#include "handlers/ready_handler.hpp"

#include <exception>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_status.hpp>

namespace tutorflow::realtime {
namespace {

std::string MakeResponse(
    const userver::server::http::HttpRequest& request, bool ready) {
  request.GetHttpResponse().SetContentType(
      userver::http::content_type::kApplicationJson);
  if (!ready) {
    request.GetHttpResponse().SetStatus(
        userver::server::http::HttpStatus::kServiceUnavailable);
  }

  userver::formats::json::ValueBuilder body(
      userver::formats::common::Type::kObject);
  body["status"] = ready ? "ready" : "not_ready";
  if (!ready) {
    userver::formats::json::ValueBuilder failed(
        userver::formats::common::Type::kArray);
    failed.PushBack("redis");
    body["failed"] = failed.ExtractValue();
  }
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace

ReadyHandler::ReadyHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      redis_(context.FindComponent<RedisClient>()) {}

std::string ReadyHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& /*context*/) const {
  try {
    redis_.Ping();
    return MakeResponse(request, true);
  } catch (const std::exception&) {
    return MakeResponse(request, false);
  }
}

}  // namespace tutorflow::realtime
