#include "domain/identity_service.hpp"

#include <chrono>
#include <cstring>
#include <sstream>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/jwt.hpp>

#include "repositories/identity_repository.hpp"

namespace tutorflow::identity {
namespace {

std::string ToHex(const unsigned char* data, size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string r;
    r.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        r += kHex[(data[i] >> 4) & 0xf];
        r += kHex[data[i] & 0xf];
    }
    return r;
}

std::vector<unsigned char> FromHex(std::string_view hex) {
    std::vector<unsigned char> r;
    r.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto h = [](char c) -> unsigned char {
            if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
            return 0;
        };
        r.push_back((h(hex[i]) << 4) | h(hex[i + 1]));
    }
    return r;
}

std::string HashPassword(std::string_view password) {
    unsigned char salt[16];
    RAND_bytes(salt, sizeof(salt));
    unsigned char hash[32];
    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                      salt, sizeof(salt),
                      100'000, EVP_sha256(),
                      sizeof(hash), hash);
    return "pbkdf2$100000$" + ToHex(salt, sizeof(salt)) + "$" +
           ToHex(hash, sizeof(hash));
}

bool VerifyPassword(std::string_view password, std::string_view stored) {
    // format: pbkdf2$<iter>$<salt_hex>$<hash_hex>
    const std::string s{stored};
    auto p1 = s.find('$');
    if (p1 == std::string::npos) return false;
    auto p2 = s.find('$', p1 + 1);
    if (p2 == std::string::npos) return false;
    auto p3 = s.find('$', p2 + 1);
    if (p3 == std::string::npos) return false;

    int iters = 0;
    try { iters = std::stoi(s.substr(p1 + 1, p2 - p1 - 1)); }
    catch (...) { return false; }

    auto salt_bytes  = FromHex(s.substr(p2 + 1, p3 - p2 - 1));
    auto expect_bytes = FromHex(s.substr(p3 + 1));
    if (expect_bytes.size() != 32) return false;

    unsigned char hash[32];
    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                      salt_bytes.data(), static_cast<int>(salt_bytes.size()),
                      iters, EVP_sha256(),
                      sizeof(hash), hash);
    return CRYPTO_memcmp(hash, expect_bytes.data(), sizeof(hash)) == 0;
}

}  // namespace

IdentityService::IdentityService(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<IdentityRepository>()),
      jwt_secret_(config["jwt-secret"].As<std::string>()),
      jwt_expires_in_(config["jwt-expires-in-seconds"].As<int64_t>(86400)) {}

// static
userver::yaml_config::Schema IdentityService::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<
        userver::components::LoggableComponentBase>(R"(
type: object
description: identity domain service
additionalProperties: false
properties:
    jwt-secret:
        type: string
        description: JWT signing secret (JWT_SECRET env)
    jwt-expires-in-seconds:
        type: integer
        description: token TTL in seconds
        defaultDescription: '86400'
)");
}

TokenResponse IdentityService::IssueToken(const User& user) const {
    auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    tutorflow::common::jwt::Claims claims;
    claims.sub   = user.id;
    claims.roles = {user.role};
    claims.iat   = now_ts;
    claims.exp   = now_ts + jwt_expires_in_;

    return TokenResponse{
        .access_token = tutorflow::common::jwt::Sign(claims, jwt_secret_),
        .expires_in   = jwt_expires_in_,
        .user_id      = user.id,
        .roles        = {user.role},
    };
}

TokenResponse IdentityService::Register(const RegisterRequest& req) const {
    if (req.email.empty() || req.password.empty() || req.display_name.empty()) {
        throw tutorflow::common::ServiceError::Validation("email, password and display_name are required");
    }
    if (req.role != "teacher" && req.role != "student") {
        throw tutorflow::common::ServiceError::Validation("role must be teacher or student");
    }
    const std::string hash = HashPassword(req.password);
    User user;
    if (req.role == "teacher") {
        user = repository_.CreateTeacher(req.email, hash, req.display_name, req.timezone);
    } else {
        user = repository_.CreateStudent(req.email, hash, req.display_name);
    }
    return IssueToken(user);
}

TokenResponse IdentityService::Login(const LoginRequest& req) const {
    if (req.email.empty() || req.password.empty()) {
        throw tutorflow::common::ServiceError::Validation("email and password are required");
    }
    auto found = repository_.FindUserWithHash(req.email);
    if (!found) {
        throw tutorflow::common::ServiceError::Unauthorized("invalid credentials");
    }
    if (!VerifyPassword(req.password, found->second)) {
        throw tutorflow::common::ServiceError::Unauthorized("invalid credentials");
    }
    return IssueToken(found->first);
}

TokenClaims IdentityService::ValidateToken(const std::string& token) const {
    auto claims = tutorflow::common::jwt::Verify(token, jwt_secret_);
    if (!claims || claims->sub.empty() || claims->roles.empty()) {
        throw tutorflow::common::ServiceError::Unauthorized(
            "missing or invalid bearer token");
    }
    return TokenClaims{
        .sub = claims->sub,
        .roles = claims->roles,
        .exp = claims->exp,
    };
}

void IdentityService::ChangePassword(
    const std::string& user_id, const ChangePasswordRequest& req) const {
    if (req.current_password.empty() || req.new_password.empty()) {
        throw tutorflow::common::ServiceError::Validation(
            "current_password and new_password are required");
    }
    if (req.new_password.size() < 8) {
        throw tutorflow::common::ServiceError::Validation(
            "new_password must be at least 8 characters");
    }
    auto found = repository_.FindUserWithHashById(user_id);
    if (!found) {
        throw tutorflow::common::ServiceError::Unauthorized("invalid credentials");
    }
    if (!VerifyPassword(req.current_password, found->second)) {
        throw tutorflow::common::ServiceError::Unauthorized("invalid credentials");
    }
    repository_.UpdatePasswordHash(user_id, HashPassword(req.new_password));
}

User IdentityService::GetUser(const std::string& user_id) const {
    auto found = repository_.FindUserById(user_id);
    if (!found) {
        throw tutorflow::common::ServiceError::NotFound("user not found");
    }
    return *found;
}

CheckAccessResult IdentityService::CheckAccess(
    const std::string& teacher_id, const std::string& student_id) const {
    return repository_.CheckAccess(teacher_id, student_id);
}

StudentLink IdentityService::CreateStudent(
    const std::string& teacher_id, const CreateStudentRequest& req) const {
    if (req.email.empty() || req.password.empty() || req.display_name.empty()) {
        throw tutorflow::common::ServiceError::Validation(
            "email, password and display_name are required");
    }
    if (req.password.size() < 8) {
        throw tutorflow::common::ServiceError::Validation(
            "password must be at least 8 characters");
    }
    return repository_.CreateStudentWithLink(teacher_id, req,
                                             HashPassword(req.password));
}

StudentLink IdentityService::GetStudentLink(
    const std::string& student_id) const {
    auto found = repository_.FindStudentLink(student_id);
    if (!found) {
        throw tutorflow::common::ServiceError::NotFound("student not found");
    }
    return *found;
}

std::vector<StudentLink> IdentityService::ListStudents(
    const std::string& teacher_id) const {
    return repository_.ListStudentsForTeacher(teacher_id);
}

}  // namespace tutorflow::identity
