#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::assignment {

class AssignmentRepository final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "assignment-repository";

  AssignmentRepository(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  Assignment CreateAssignment(const std::string &teacher_id,
                              const CreateAssignmentRequest &request) const;
  std::vector<Assignment>
  ListAssignmentsForTeacher(const std::string &teacher_id) const;
  std::vector<Assignment>
  ListAssignmentsForStudent(const std::string &student_id) const;
  std::optional<Assignment>
  FindAssignment(const std::string &assignment_id) const;
  AssignmentDetail GetAssignmentDetail(const std::string &assignment_id) const;
  Submission CreateSubmission(const std::string &assignment_id,
                              const std::string &student_id,
                              const SubmitRequest &request) const;
  Submission ReviewLatestSubmission(const std::string &assignment_id,
                                    const ReviewRequest &request) const;
  Comment CreateComment(const std::string &assignment_id,
                        const std::string &author_id,
                        const CommentRequest &request) const;

private:
  userver::storages::postgres::ClusterPtr pg_;
};

} // namespace tutorflow::assignment
