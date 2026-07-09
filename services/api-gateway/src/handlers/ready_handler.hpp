#pragma once

#include <string_view>

#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::gateway {

class ReadyHandler final : public userver::server::handlers::HttpHandlerBase {
 public:
  static constexpr std::string_view kName = "ready-handler";

  using HttpHandlerBase::HttpHandlerBase;

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest& request,
      userver::server::request::RequestContext& context) const override;
};

}  // namespace tutorflow::gateway
