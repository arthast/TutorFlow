#include "grpc/assignment_grpc_service.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

#include "domain/models.hpp"

namespace tutorflow::assignment {
namespace {
namespace proto = tutorflow::assignment::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

std::vector<std::string> ToVector(
    const google::protobuf::RepeatedPtrField<std::string> &items) {
  std::vector<std::string> result;
  result.reserve(items.size());
  for (const auto &item : items) {
    result.push_back(item);
  }
  return result;
}

proto::Assignment ToProto(const Assignment &assignment) {
  proto::Assignment response;
  response.set_id(assignment.id);
  response.set_teacher_id(assignment.teacher_id);
  response.set_student_id(assignment.student_id);
  response.set_title(assignment.title);
  if (assignment.description) {
    response.set_description(*assignment.description);
  }
  if (assignment.due_at) {
    response.set_due_at(*assignment.due_at);
  }
  response.set_status(assignment.status);
  response.set_created_at(assignment.created_at);
  return response;
}

proto::Submission ToProto(const Submission &submission) {
  proto::Submission response;
  response.set_id(submission.id);
  response.set_assignment_id(submission.assignment_id);
  response.set_student_id(submission.student_id);
  if (submission.text_answer) {
    response.set_text_answer(*submission.text_answer);
  }
  response.set_status(submission.status);
  response.set_submitted_at(submission.submitted_at);
  for (const auto &file_id : submission.file_ids) {
    response.add_file_ids(file_id);
  }
  return response;
}

proto::Comment ToProto(const Comment &comment) {
  proto::Comment response;
  response.set_id(comment.id);
  response.set_assignment_id(comment.assignment_id);
  response.set_author_id(comment.author_id);
  response.set_text(comment.text);
  response.set_created_at(comment.created_at);
  return response;
}

proto::AssignmentDetail ToProto(const AssignmentDetail &detail) {
  proto::AssignmentDetail response;
  *response.mutable_assignment() = ToProto(detail.assignment);
  for (const auto &file_id : detail.file_ids) {
    response.add_file_ids(file_id);
  }
  for (const auto &submission : detail.submissions) {
    *response.add_submissions() = ToProto(submission);
  }
  for (const auto &comment : detail.comments) {
    *response.add_comments() = ToProto(comment);
  }
  return response;
}

CreateAssignmentRequest
FromProto(const proto::CreateAssignmentRequest &request) {
  return CreateAssignmentRequest{
      .student_id = request.student_id(),
      .title = request.title(),
      .description = request.has_description()
                         ? std::optional<std::string>{request.description()}
                         : std::nullopt,
      .due_at = request.has_due_at()
                    ? std::optional<std::string>{request.due_at()}
                    : std::nullopt,
      .file_ids = ToVector(request.file_ids()),
  };
}

SubmitRequest FromProto(const proto::SubmitAssignmentRequest &request) {
  return SubmitRequest{
      .text_answer = request.has_text_answer()
                         ? std::optional<std::string>{request.text_answer()}
                         : std::nullopt,
      .file_ids = ToVector(request.file_ids()),
  };
}

ReviewRequest FromProto(const proto::ReviewAssignmentRequest &request) {
  return ReviewRequest{
      .status = request.status(),
      .comment = request.has_comment()
                     ? std::optional<std::string>{request.comment()}
                     : std::nullopt,
  };
}

CommentRequest FromProto(const proto::AddCommentRequest &request) {
  return CommentRequest{.text = request.text()};
}

} // namespace

AssignmentGrpcService::AssignmentGrpcService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : proto::AssignmentServiceBase::Component(config, context),
      service_(context.FindComponent<AssignmentService>()) {}

AssignmentGrpcService::CreateAssignmentResult
AssignmentGrpcService::CreateAssignment(
    CallContext &context, proto::CreateAssignmentRequest &&request) {
  return InvokeServerUnary<proto::Assignment>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CreateAssignment(auth, FromProto(request)));
  });
}

AssignmentGrpcService::ListAssignmentsResult
AssignmentGrpcService::ListAssignments(
    CallContext &context, proto::ListAssignmentsRequest &&request) {
  return InvokeServerUnary<proto::ListAssignmentsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListAssignmentsResponse response;
    for (const auto &assignment : service_.ListAssignments(auth)) {
      *response.add_assignments() = ToProto(assignment);
    }
    return response;
  });
}

AssignmentGrpcService::GetAssignmentResult
AssignmentGrpcService::GetAssignment(CallContext &context,
                                     proto::GetAssignmentRequest &&request) {
  return InvokeServerUnary<proto::AssignmentDetail>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.GetAssignment(auth, request.assignment_id()));
  });
}

AssignmentGrpcService::SubmitAssignmentResult
AssignmentGrpcService::SubmitAssignment(
    CallContext &context, proto::SubmitAssignmentRequest &&request) {
  return InvokeServerUnary<proto::Submission>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.SubmitAssignment(auth, request.assignment_id(),
                                             FromProto(request)));
  });
}

AssignmentGrpcService::ReviewAssignmentResult
AssignmentGrpcService::ReviewAssignment(
    CallContext &context, proto::ReviewAssignmentRequest &&request) {
  return InvokeServerUnary<proto::Submission>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.ReviewAssignment(auth, request.assignment_id(),
                                             FromProto(request)));
  });
}

AssignmentGrpcService::AddCommentResult
AssignmentGrpcService::AddComment(CallContext &context,
                                  proto::AddCommentRequest &&request) {
  return InvokeServerUnary<proto::Comment>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.CreateComment(auth, request.assignment_id(),
                                          FromProto(request)));
  });
}

} // namespace tutorflow::assignment
