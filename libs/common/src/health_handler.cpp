#include <tutorflow/common/health_handler.hpp>

#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>

namespace tutorflow::common {

std::string HealthHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& /*context*/) const {
  request.GetHttpResponse().SetContentType(
      userver::http::content_type::kApplicationJson);

  userver::formats::json::ValueBuilder body(
      userver::formats::common::Type::kObject);
  body["status"] = "ok";
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace tutorflow::common
