#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::identity {

class IdentityRepository final
    : public userver::components::LoggableComponentBase {
public:
    static constexpr std::string_view kName = "identity-repository";

    IdentityRepository(const userver::components::ComponentConfig& config,
                       const userver::components::ComponentContext& context);

    User CreateTeacher(const std::string& email,
                       const std::string& password_hash,
                       const std::string& display_name,
                       const std::optional<std::string>& timezone) const;

    User CreateStudent(const std::string& email,
                       const std::string& password_hash,
                       const std::string& display_name) const;

    // Returns {User, password_hash} or nullopt if not found.
    std::optional<std::pair<User, std::string>> FindUserWithHash(
        const std::string& email) const;

    // Returns {User, password_hash} or nullopt if not found.
    std::optional<std::pair<User, std::string>> FindUserWithHashById(
        const std::string& id) const;

    void UpdatePasswordHash(const std::string& user_id,
                            const std::string& password_hash) const;

    std::optional<User> FindUserById(const std::string& id) const;

    CheckAccessResult CheckAccess(const std::string& teacher_id,
                                  const std::string& student_id) const;

    StudentLink CreateStudentWithLink(const std::string& teacher_id,
                                     const CreateStudentRequest& req,
                                     const std::string& password_hash) const;

    std::optional<StudentLink> FindStudentLink(
        const std::string& student_id) const;

    std::vector<StudentLink> ListStudentsForTeacher(
        const std::string& teacher_id) const;

private:
    userver::storages::postgres::ClusterPtr pg_;
};

}  // namespace tutorflow::identity
