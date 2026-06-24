#pragma once

#include <cstdint>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include "domain/models.hpp"

namespace tutorflow::identity {

class IdentityRepository;

class IdentityService final
    : public userver::components::LoggableComponentBase {
public:
    static constexpr std::string_view kName = "identity-domain-service";

    IdentityService(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

    static userver::yaml_config::Schema GetStaticConfigSchema();

    TokenResponse Register(const RegisterRequest& req) const;
    TokenResponse Login(const LoginRequest& req) const;
    TokenClaims ValidateToken(const std::string& token) const;
    void ChangePassword(const std::string& user_id,
                        const ChangePasswordRequest& req) const;

    User GetUser(const std::string& user_id) const;
    CheckAccessResult CheckAccess(const std::string& teacher_id,
                                  const std::string& student_id) const;
    StudentLink CreateStudent(const std::string& teacher_id,
                              const CreateStudentRequest& req) const;
    StudentLink GetStudentLink(const std::string& student_id) const;
    std::vector<StudentLink> ListStudents(const std::string& teacher_id) const;

private:
    TokenResponse IssueToken(const User& user) const;

    IdentityRepository& repository_;
    std::string jwt_secret_;
    int64_t jwt_expires_in_{};
};

}  // namespace tutorflow::identity
