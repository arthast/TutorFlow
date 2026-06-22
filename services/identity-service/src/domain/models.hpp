#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/formats/json/value.hpp>

namespace tutorflow::identity {

struct User {
    std::string id;
    std::string email;
    std::string role;
    std::string display_name;
    std::string created_at;
};

struct TokenResponse {
    std::string access_token;
    std::string token_type{"Bearer"};
    int64_t expires_in{};
    std::string user_id;
    std::vector<std::string> roles;
};

struct CheckAccessResult {
    bool allowed{};
    std::string status;
    std::optional<double> hourly_rate;
};

struct StudentLink {
    std::string id;
    std::string teacher_id;
    std::string student_id;
    std::string display_name;
    std::optional<std::string> subject;
    std::optional<std::string> goal;
    std::optional<double> hourly_rate;
    std::string status;
    std::string created_at;
};

struct RegisterRequest {
    std::string email;
    std::string password;
    std::string role;
    std::string display_name;
    std::optional<std::string> timezone;
};

struct LoginRequest {
    std::string email;
    std::string password;
};

struct CreateStudentRequest {
    std::optional<std::string> email;
    std::string display_name;
    std::optional<std::string> subject;
    std::optional<std::string> goal;
    std::optional<double> hourly_rate;
};

userver::formats::json::Value ToJson(const User& u);
userver::formats::json::Value ToJson(const TokenResponse& t);
userver::formats::json::Value ToJson(const CheckAccessResult& r);
userver::formats::json::Value ToJson(const StudentLink& s);

}  // namespace tutorflow::identity
