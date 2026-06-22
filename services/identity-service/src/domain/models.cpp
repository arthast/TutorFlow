#include "domain/models.hpp"

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::identity {
namespace {
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
}  // namespace

json::Value ToJson(const User& u) {
    json::ValueBuilder b;
    b["id"] = u.id;
    b["email"] = u.email;
    b["role"] = u.role;
    b["display_name"] = u.display_name;
    b["created_at"] = u.created_at;
    return b.ExtractValue();
}

json::Value ToJson(const TokenResponse& t) {
    json::ValueBuilder b;
    b["access_token"] = t.access_token;
    b["token_type"] = t.token_type;
    b["expires_in"] = t.expires_in;
    b["user_id"] = t.user_id;
    json::ValueBuilder roles(common_formats::Type::kArray);
    for (const auto& r : t.roles) roles.PushBack(r);
    b["roles"] = roles.ExtractValue();
    return b.ExtractValue();
}

json::Value ToJson(const CheckAccessResult& r) {
    json::ValueBuilder b;
    b["allowed"] = r.allowed;
    if (r.allowed) {
        b["status"] = r.status;
        if (r.hourly_rate.has_value()) {
            b["hourly_rate"] = *r.hourly_rate;
        } else {
            b["hourly_rate"] = nullptr;
        }
    } else {
        b["status"] = nullptr;
        b["hourly_rate"] = nullptr;
    }
    return b.ExtractValue();
}

json::Value ToJson(const StudentLink& s) {
    json::ValueBuilder b;
    b["id"] = s.id;
    b["teacher_id"] = s.teacher_id;
    b["student_id"] = s.student_id;
    b["display_name"] = s.display_name;
    b["subject"] = s.subject.has_value() ? json::ValueBuilder(*s.subject).ExtractValue()
                                         : json::ValueBuilder(nullptr).ExtractValue();
    b["goal"] = s.goal.has_value() ? json::ValueBuilder(*s.goal).ExtractValue()
                                   : json::ValueBuilder(nullptr).ExtractValue();
    b["hourly_rate"] = s.hourly_rate.has_value()
                           ? json::ValueBuilder(*s.hourly_rate).ExtractValue()
                           : json::ValueBuilder(nullptr).ExtractValue();
    b["status"] = s.status;
    b["created_at"] = s.created_at;
    return b.ExtractValue();
}

}  // namespace tutorflow::identity
