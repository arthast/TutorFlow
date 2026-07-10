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

namespace tutorflow::chat {
namespace {
namespace pg = userver::storages::postgres;

std::string MakeResponse(
    const userver::server::http::HttpRequest& request,
    const std::vector<std::string>& failed_dependencies) {
  const bool ready = failed_dependencies.empty();
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
    for (const auto& dependency : failed_dependencies) {
      failed.PushBack(dependency);
    }
    body["failed"] = failed.ExtractValue();
  }
  return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace

ReadyHandler::ReadyHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      clusters_({
          context.FindComponent<userver::components::Postgres>(
                     "chat-db-shard0")
              .GetCluster(),
          context.FindComponent<userver::components::Postgres>(
                     "chat-db-shard1")
              .GetCluster(),
      }) {}

std::string ReadyHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& /*context*/) const {
  std::vector<std::string> failed_dependencies;
  for (std::size_t i = 0; i < clusters_.size(); ++i) {
    try {
      clusters_[i]->Execute(pg::ClusterHostType::kMaster, "SELECT 1");
    } catch (const std::exception&) {
      failed_dependencies.push_back("postgres_shard" + std::to_string(i));
    }
  }
  return MakeResponse(request, failed_dependencies);
}

}  // namespace tutorflow::chat
