#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include <userver/formats/json/value.hpp>
#include <userver/server/http/http_status.hpp>

// Единый формат ошибок (PLAN §6):
//   { "error": { "code": "...", "message": "...", "details": {} } }
//
// Доменный/handler код бросает ServiceError; на границе HTTP он превращается в
// envelope с нужным статусом. Коды — из error_codes.hpp.
namespace tutorflow::common {

class ServiceError : public std::runtime_error {
 public:
  ServiceError(userver::server::http::HttpStatus status, std::string code,
               std::string message,
               userver::formats::json::Value details = {});

  userver::server::http::HttpStatus Status() const noexcept { return status_; }
  const std::string& Code() const noexcept { return code_; }
  const userver::formats::json::Value& Details() const noexcept {
    return details_;
  }

  // Фабрики под каноническое соответствие статус<->код (PLAN §6).
  static ServiceError Validation(std::string message,
                                 userver::formats::json::Value details = {});
  static ServiceError Unauthorized(std::string message = "unauthorized");
  static ServiceError Forbidden(std::string message = "forbidden");
  static ServiceError NotFound(std::string message = "not found");
  static ServiceError Conflict(std::string message);
  static ServiceError BusinessRule(std::string message,
                                   userver::formats::json::Value details = {});
  static ServiceError Internal(std::string message = "internal error");

 private:
  userver::server::http::HttpStatus status_;
  std::string code_;
  userver::formats::json::Value details_;
};

// Собрать тело-envelope. details == null -> пустой объект.
userver::formats::json::Value MakeErrorBody(
    std::string_view code, std::string_view message,
    userver::formats::json::Value details = {});

userver::formats::json::Value MakeErrorBody(const ServiceError& error);

}  // namespace tutorflow::common
