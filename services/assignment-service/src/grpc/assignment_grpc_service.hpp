#pragma once

#include <tutorflow/assignment_service.usrv.pb.hpp>

#include "domain/assignment_service.hpp"

namespace tutorflow::assignment {

class AssignmentGrpcService final
    : public tutorflow::assignment::v1::AssignmentServiceBase::Component {
public:
  static constexpr std::string_view kName = "assignment-grpc-service";

  AssignmentGrpcService(const userver::components::ComponentConfig &config,
                        const userver::components::ComponentContext &context);

  CreateAssignmentResult CreateAssignment(
      CallContext &context,
      tutorflow::assignment::v1::CreateAssignmentRequest &&request) override;
  ListAssignmentsResult ListAssignments(
      CallContext &context,
      tutorflow::assignment::v1::ListAssignmentsRequest &&request) override;
  GetAssignmentResult
  GetAssignment(CallContext &context,
                tutorflow::assignment::v1::GetAssignmentRequest &&request)
      override;
  SubmitAssignmentResult SubmitAssignment(
      CallContext &context,
      tutorflow::assignment::v1::SubmitAssignmentRequest &&request) override;
  ReviewAssignmentResult ReviewAssignment(
      CallContext &context,
      tutorflow::assignment::v1::ReviewAssignmentRequest &&request) override;
  AddCommentResult
  AddComment(CallContext &context,
             tutorflow::assignment::v1::AddCommentRequest &&request) override;

private:
  AssignmentService &service_;
};

} // namespace tutorflow::assignment
