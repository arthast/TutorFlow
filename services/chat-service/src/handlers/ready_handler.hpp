#pragma once

#include <string_view>
#include <vector>

#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

namespace tutorflow::chat {

class ReadyHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "ready-handler";

  ReadyHandler(const userver::components::ComponentConfig& config,
               const userver::components::ComponentContext& context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext& context) const override;

 private:
  std::vector<userver::storages::postgres::ClusterPtr> clusters_;
};

}  // namespace tutorflow::chat
