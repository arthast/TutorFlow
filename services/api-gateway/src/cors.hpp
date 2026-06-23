#pragma once

#include <string>

#include <userver/server/http/http_request.hpp>

#include "gateway_settings.hpp"

namespace tutorflow::gateway {

void ApplyCorsHeaders(const userver::server::http::HttpRequest& request,
                      const GatewaySettings& settings);

bool IsOptionsRequest(const userver::server::http::HttpRequest& request);

std::string MakePreflightResponse(
    const userver::server::http::HttpRequest& request,
    const GatewaySettings& settings);

}  // namespace tutorflow::gateway
