#pragma once

#include <string>
#include <string_view>

#include <userver/server/http/http_request.hpp>

// Correlation-id для сквозной трассировки запроса между сервисами (PLAN §7).
// Задел под трейсинг: id берём из заголовка или генерируем и пробрасываем дальше
// через HttpClientBase.
namespace tutorflow::common {

inline constexpr std::string_view kHeaderRequestId = "X-Request-Id";

std::string GenerateRequestId();

// Возвращает X-Request-Id из запроса или генерирует новый, если его нет.
std::string GetOrCreateRequestId(
    const userver::server::http::HttpRequest& request);

}  // namespace tutorflow::common
