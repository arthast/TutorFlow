#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include <userver/clients/http/client.hpp>
#include <userver/formats/json/value.hpp>

// Базовый клиент к другому внутреннему сервису (PLAN §7, §10).
// Оборачивает userver http-client: базовый URL из конфига, проброс X-User-* и
// X-Request-Id, единый разбор error-envelope в ServiceError.
//
// Конкретные клиенты (identity, file, finance, ...) наследуются и добавляют
// типобезопасные методы; здесь — только транспорт. DTO сюда не кладём.
namespace tutorflow::common {

// Что пробрасываем дальше по цепочке вызовов между сервисами.
struct CallContext {
  std::string user_id;     // -> X-User-Id (если не пусто)
  std::string roles;       // -> X-User-Roles, CSV (если не пусто)
  std::string request_id;  // -> X-Request-Id (correlation)
};

class HttpClientBase {
 public:
  HttpClientBase(userver::clients::http::Client& http_client,
                 std::string base_url,
                 std::chrono::milliseconds timeout = std::chrono::seconds{5});

  virtual ~HttpClientBase() = default;

  userver::formats::json::Value GetJson(std::string_view path,
                                        const CallContext& ctx) const;

  userver::formats::json::Value PostJson(
      std::string_view path, const userver::formats::json::Value& body,
      const CallContext& ctx) const;

 protected:
  std::string Url(std::string_view path) const;

  // При статусе вне 2xx бросает ServiceError, восстановленный из error-envelope.
  userver::formats::json::Value ParseResponse(int status_code,
                                              const std::string& body) const;

  userver::clients::http::Headers BuildHeaders(const CallContext& ctx,
                                               bool json_body) const;

 private:
  userver::clients::http::Client& http_client_;
  std::string base_url_;
  std::chrono::milliseconds timeout_;
};

}  // namespace tutorflow::common
