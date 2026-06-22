#include <tutorflow/common/auth_context.hpp>

#include <algorithm>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::common {
namespace {

std::string Trim(std::string_view s) {
  const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && is_space(s[begin])) ++begin;
  while (end > begin && is_space(s[end - 1])) --end;
  return std::string{s.substr(begin, end - begin)};
}

std::vector<std::string> SplitCsv(std::string_view csv) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (pos <= csv.size()) {
    const auto comma = csv.find(',', pos);
    const auto chunk =
        csv.substr(pos, comma == std::string_view::npos ? std::string_view::npos
                                                        : comma - pos);
    auto trimmed = Trim(chunk);
    if (!trimmed.empty()) out.push_back(std::move(trimmed));
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return out;
}

}  // namespace

bool AuthContext::HasRole(std::string_view role) const {
  return std::any_of(roles.begin(), roles.end(),
                     [&](const std::string& r) { return r == role; });
}

AuthContext ParseAuthContext(
    const userver::server::http::HttpRequest& request) {
  AuthContext ctx;
  ctx.user_id = request.GetHeader(std::string{kHeaderUserId});
  if (ctx.user_id.empty()) {
    throw ServiceError::Unauthorized("missing X-User-Id");
  }
  ctx.roles = SplitCsv(request.GetHeader(std::string{kHeaderUserRoles}));
  return ctx;
}

void RequireRole(const AuthContext& ctx, std::string_view role) {
  if (!ctx.HasRole(role)) {
    throw ServiceError::Forbidden("role '" + std::string{role} + "' required");
  }
}

void RequireTeacher(const AuthContext& ctx) { RequireRole(ctx, "teacher"); }
void RequireStudent(const AuthContext& ctx) { RequireRole(ctx, "student"); }

}  // namespace tutorflow::common
