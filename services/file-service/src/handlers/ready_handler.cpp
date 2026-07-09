#include "handlers/ready_handler.hpp"

#include <exception>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>

namespace tutorflow::file {
namespace {
namespace pg = userver::storages::postgres;

std::string MakeResponse(const userver::server::http::HttpRequest& request,
                         const std::vector<std::string>& failed) {
  request.GetHttpResponse().SetContentType(
      userver::http::content_type::kApplicationJson);
  const bool ready = failed.empty();
  if (!ready) {
    request.GetHttpResponse().SetStatus(
        userver::server::http::HttpStatus::kServiceUnavailable);
  }

  userver::formats::json::ValueBuilder body(
      userver::formats::common::Type::kObject);
  body["status"] = ready ? "ready" : "not_ready";
  if (!ready) {
    userver::formats::json::ValueBuilder failed_json(
        userver::formats::common::Type::kArray);
    for (const auto& item : failed) {
      failed_json.PushBack(item);
    }
    body["failed"] = failed_json.ExtractValue();
  }
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace

ReadyHandler::ReadyHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("file-db")
              .GetCluster()),
      storage_(context.FindComponent<FileStorageComponent>()) {}

std::string ReadyHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& /*context*/) const {
  std::vector<std::string> failed;
  try {
    pg_->Execute(pg::ClusterHostType::kMaster, "SELECT 1");
  } catch (const std::exception&) {
    failed.push_back("postgres");
  }

  try {
    storage_.CheckReady();
  } catch (const std::exception&) {
    failed.push_back("storage");
  }

  return MakeResponse(request, failed);
}

}  // namespace tutorflow::file
