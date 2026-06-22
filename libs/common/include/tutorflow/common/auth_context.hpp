#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <userver/server/http/http_request.hpp>

// Контекст пользователя из заголовков, которые проставляет api-gateway после
// валидации JWT (PLAN §5). Внутренние сервисы JWT не валидируют — доверяют
// X-User-* от gateway и проверяют только бизнес-доступ.
namespace tutorflow::common {

inline constexpr std::string_view kHeaderUserId = "X-User-Id";
inline constexpr std::string_view kHeaderUserRoles = "X-User-Roles";

struct AuthContext {
  std::string user_id;
  // MVP: ровно одна роль, но формат уже под мультироль (PLAN §1, §5).
  std::vector<std::string> roles;

  bool HasRole(std::string_view role) const;
  bool IsTeacher() const { return HasRole("teacher"); }
  bool IsStudent() const { return HasRole("student"); }
};

// Парсит X-User-Id / X-User-Roles. Бросает ServiceError::Unauthorized,
// если X-User-Id отсутствует.
AuthContext ParseAuthContext(const userver::server::http::HttpRequest& request);

// require-хелперы: бросают ServiceError::Forbidden, если роль не совпала.
void RequireRole(const AuthContext& ctx, std::string_view role);
void RequireTeacher(const AuthContext& ctx);
void RequireStudent(const AuthContext& ctx);

}  // namespace tutorflow::common
