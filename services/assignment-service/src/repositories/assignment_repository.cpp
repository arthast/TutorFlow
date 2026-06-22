#include "repositories/assignment_repository.hpp"

#include <optional>

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::assignment {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

constexpr std::string_view kAssignmentFields = R"(
  id::text,
  teacher_id::text,
  student_id::text,
  title,
  COALESCE(description, '') AS description,
  COALESCE(to_char(due_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS due_at,
  status,
  to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

constexpr std::string_view kSubmissionFields = R"(
  id::text,
  assignment_id::text,
  student_id::text,
  COALESCE(text_answer, '') AS text_answer,
  status,
  to_char(submitted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS submitted_at
)";

constexpr std::string_view kCommentFields = R"(
  id::text,
  assignment_id::text,
  author_id::text,
  text,
  to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

std::optional<std::string> EmptyToNull(std::string value) {
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

Assignment RowToAssignment(const pg::Row &row) {
  return Assignment{
      row["id"].As<std::string>(),
      row["teacher_id"].As<std::string>(),
      row["student_id"].As<std::string>(),
      row["title"].As<std::string>(),
      EmptyToNull(row["description"].As<std::string>()),
      EmptyToNull(row["due_at"].As<std::string>()),
      row["status"].As<std::string>(),
      row["created_at"].As<std::string>(),
  };
}

Submission RowToSubmission(const pg::Row &row) {
  return Submission{
      row["id"].As<std::string>(),
      row["assignment_id"].As<std::string>(),
      row["student_id"].As<std::string>(),
      EmptyToNull(row["text_answer"].As<std::string>()),
      row["status"].As<std::string>(),
      row["submitted_at"].As<std::string>(),
      {},
  };
}

Comment RowToComment(const pg::Row &row) {
  return Comment{
      row["id"].As<std::string>(),
      row["assignment_id"].As<std::string>(),
      row["author_id"].As<std::string>(),
      row["text"].As<std::string>(),
      row["created_at"].As<std::string>(),
  };
}

template <typename T>
std::vector<T> RowsToVector(const pg::ResultSet &result,
                            T (*mapper)(const pg::Row &)) {
  std::vector<T> items;
  items.reserve(result.Size());
  for (const auto &row : result) {
    items.push_back(mapper(row));
  }
  return items;
}

Assignment RequireSingleAssignment(const pg::ResultSet &result) {
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound("assignment not found");
  }
  return RowToAssignment(result[0]);
}

Submission RequireSingleSubmission(const pg::ResultSet &result) {
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound("submission not found");
  }
  return RowToSubmission(result[0]);
}

std::vector<std::string> LoadAssignmentFileIds(
    const userver::storages::postgres::ClusterPtr &pg,
    const std::string &assignment_id) {
  const auto result =
      pg->Execute(kSlave,
                  "SELECT file_id::text FROM assignment_files "
                  "WHERE assignment_id = $1::uuid ORDER BY created_at, id",
                  assignment_id);
  std::vector<std::string> ids;
  ids.reserve(result.Size());
  for (const auto &row : result) {
    ids.push_back(row["file_id"].As<std::string>());
  }
  return ids;
}

std::vector<std::string> LoadSubmissionFileIds(
    const userver::storages::postgres::ClusterPtr &pg,
    const std::string &submission_id) {
  const auto result =
      pg->Execute(kSlave,
                  "SELECT file_id::text FROM submission_files "
                  "WHERE submission_id = $1::uuid ORDER BY created_at, id",
                  submission_id);
  std::vector<std::string> ids;
  ids.reserve(result.Size());
  for (const auto &row : result) {
    ids.push_back(row["file_id"].As<std::string>());
  }
  return ids;
}

void InsertAssignmentFiles(
    const userver::storages::postgres::ClusterPtr &pg,
    const std::string &assignment_id, const std::vector<std::string> &file_ids) {
  for (const auto &file_id : file_ids) {
    pg->Execute(kMaster,
                "INSERT INTO assignment_files (assignment_id, file_id) "
                "VALUES ($1::uuid, $2::uuid) ON CONFLICT DO NOTHING",
                assignment_id, file_id);
  }
}

void InsertSubmissionFiles(
    const userver::storages::postgres::ClusterPtr &pg,
    const std::string &submission_id, const std::vector<std::string> &file_ids) {
  for (const auto &file_id : file_ids) {
    pg->Execute(kMaster,
                "INSERT INTO submission_files (submission_id, file_id) "
                "VALUES ($1::uuid, $2::uuid) ON CONFLICT DO NOTHING",
                submission_id, file_id);
  }
}

std::string AssignmentStatusForReviewStatus(const std::string &status) {
  if (status == "accepted") {
    return "done";
  }
  return status;
}

} // namespace

AssignmentRepository::AssignmentRepository(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("assignment-db")
              .GetCluster()) {}

Assignment AssignmentRepository::CreateAssignment(
    const std::string &teacher_id,
    const CreateAssignmentRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "INSERT INTO assignments "
      "  (teacher_id, student_id, title, description, due_at) "
      "VALUES ($1::uuid, $2::uuid, $3, $4, NULLIF($5, '')::timestamptz) "
      "RETURNING " +
          std::string{kAssignmentFields},
      teacher_id, request.student_id, request.title, request.description,
      request.due_at.value_or(""));
  auto assignment = RequireSingleAssignment(result);
  InsertAssignmentFiles(pg_, assignment.id, request.file_ids);
  return assignment;
}

std::vector<Assignment> AssignmentRepository::ListAssignmentsForTeacher(
    const std::string &teacher_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kAssignmentFields} +
                       " FROM assignments WHERE teacher_id = $1::uuid "
                       "ORDER BY created_at DESC, id DESC",
                   teacher_id);
  return RowsToVector<Assignment>(result, RowToAssignment);
}

std::vector<Assignment> AssignmentRepository::ListAssignmentsForStudent(
    const std::string &student_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kAssignmentFields} +
                       " FROM assignments WHERE student_id = $1::uuid "
                       "ORDER BY created_at DESC, id DESC",
                   student_id);
  return RowsToVector<Assignment>(result, RowToAssignment);
}

std::optional<Assignment>
AssignmentRepository::FindAssignment(const std::string &assignment_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kAssignmentFields} +
                       " FROM assignments WHERE id = $1::uuid",
                   assignment_id);
  if (result.IsEmpty()) {
    return std::nullopt;
  }
  return RowToAssignment(result[0]);
}

AssignmentDetail
AssignmentRepository::GetAssignmentDetail(const std::string &assignment_id) const {
  const auto assignment_result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kAssignmentFields} +
                       " FROM assignments WHERE id = $1::uuid",
                   assignment_id);
  auto assignment = RequireSingleAssignment(assignment_result);

  auto file_ids = LoadAssignmentFileIds(pg_, assignment_id);

  auto submissions =
      RowsToVector<Submission>(
          pg_->Execute(kSlave,
                       "SELECT " + std::string{kSubmissionFields} +
                           " FROM submissions WHERE assignment_id = $1::uuid "
                           "ORDER BY submitted_at DESC, id DESC",
                       assignment_id),
          RowToSubmission);
  for (auto &submission : submissions) {
    submission.file_ids = LoadSubmissionFileIds(pg_, submission.id);
  }

  auto comments =
      RowsToVector<Comment>(
          pg_->Execute(kSlave,
                       "SELECT " + std::string{kCommentFields} +
                           " FROM assignment_comments "
                           "WHERE assignment_id = $1::uuid "
                           "ORDER BY created_at, id",
                       assignment_id),
          RowToComment);

  return AssignmentDetail{.assignment = std::move(assignment),
                          .file_ids = std::move(file_ids),
                          .submissions = std::move(submissions),
                          .comments = std::move(comments)};
}

Submission AssignmentRepository::CreateSubmission(
    const std::string &assignment_id, const std::string &student_id,
    const SubmitRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH inserted AS ("
      "  INSERT INTO submissions (assignment_id, student_id, text_answer) "
      "  VALUES ($1::uuid, $2::uuid, $3) "
      "  RETURNING " +
          std::string{kSubmissionFields} +
          "), assignment_update AS ("
          "  UPDATE assignments SET status = 'submitted' "
          "  WHERE id = $1::uuid RETURNING id"
          ") SELECT * FROM inserted",
      assignment_id, student_id, request.text_answer);
  auto submission = RequireSingleSubmission(result);
  InsertSubmissionFiles(pg_, submission.id, request.file_ids);
  submission.file_ids = LoadSubmissionFileIds(pg_, submission.id);
  return submission;
}

Submission AssignmentRepository::ReviewLatestSubmission(
    const std::string &assignment_id, const ReviewRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH latest AS ("
      "  SELECT id FROM submissions WHERE assignment_id = $1::uuid "
      "  ORDER BY submitted_at DESC, id DESC LIMIT 1"
      "), updated_submission AS ("
      "  UPDATE submissions SET status = $2 "
      "  WHERE id IN (SELECT id FROM latest) "
      "  RETURNING " +
          std::string{kSubmissionFields} +
          "), updated_assignment AS ("
          "  UPDATE assignments SET status = $3 "
          "  WHERE id = $1::uuid AND EXISTS (SELECT 1 FROM updated_submission) "
          "  RETURNING id"
          "), inserted_comment AS ("
          "  INSERT INTO assignment_comments (assignment_id, author_id, text) "
          "  SELECT $1::uuid, teacher_id, $4 FROM assignments "
          "  WHERE id = $1::uuid AND $4 <> '' "
          "  RETURNING id"
          ") SELECT * FROM updated_submission",
      assignment_id, request.status, AssignmentStatusForReviewStatus(request.status),
      request.comment.value_or(""));
  auto submission = RequireSingleSubmission(result);
  submission.file_ids = LoadSubmissionFileIds(pg_, submission.id);
  return submission;
}

Comment AssignmentRepository::CreateComment(
    const std::string &assignment_id, const std::string &author_id,
    const CommentRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "INSERT INTO assignment_comments (assignment_id, author_id, text) "
      "VALUES ($1::uuid, $2::uuid, $3) RETURNING " +
          std::string{kCommentFields},
      assignment_id, author_id, request.text);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound("assignment not found");
  }
  return RowToComment(result[0]);
}

} // namespace tutorflow::assignment
