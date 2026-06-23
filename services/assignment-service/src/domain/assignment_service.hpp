#pragma once

#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include "domain/models.hpp"

namespace tutorflow::common {
struct AuthContext;
}

namespace tutorflow::assignment {

class AssignmentRepository;
class IdentityClient;

class AssignmentService final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "assignment-domain-service";

  AssignmentService(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  Assignment CreateAssignment(const tutorflow::common::AuthContext &auth,
                              const CreateAssignmentRequest &request) const;
  std::vector<Assignment>
  ListAssignments(const tutorflow::common::AuthContext &auth) const;
  AssignmentDetail GetAssignment(const tutorflow::common::AuthContext &auth,
                                 const std::string &assignment_id) const;
  Submission SubmitAssignment(const tutorflow::common::AuthContext &auth,
                              const std::string &assignment_id,
                              const SubmitRequest &request) const;
  Submission ReviewAssignment(const tutorflow::common::AuthContext &auth,
                              const std::string &assignment_id,
                              const ReviewRequest &request) const;
  Comment CreateComment(const tutorflow::common::AuthContext &auth,
                        const std::string &assignment_id,
                        const CommentRequest &request) const;

private:
  AssignmentRepository &repository_;
  IdentityClient &identity_;
};

} // namespace tutorflow::assignment
