#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/formats/json/value.hpp>

namespace tutorflow::assignment {

struct Assignment {
  std::string id;
  std::string teacher_id;
  std::string student_id;
  std::string title;
  std::optional<std::string> description;
  std::optional<std::string> due_at;
  std::string status;
  std::string created_at;
};

struct Submission {
  std::string id;
  std::string assignment_id;
  std::string student_id;
  std::optional<std::string> text_answer;
  std::string status;
  std::string submitted_at;
  std::vector<std::string> file_ids;
};

struct Comment {
  std::string id;
  std::string assignment_id;
  std::string author_id;
  std::string text;
  std::string created_at;
};

struct AssignmentDetail {
  Assignment assignment;
  std::vector<std::string> file_ids;
  std::vector<Submission> submissions;
  std::vector<Comment> comments;
};

struct CreateAssignmentRequest {
  std::string student_id;
  std::string title;
  std::optional<std::string> description;
  std::optional<std::string> due_at;
  std::vector<std::string> file_ids;
};

struct SubmitRequest {
  std::optional<std::string> text_answer;
  std::vector<std::string> file_ids;
};

struct ReviewRequest {
  std::string status;
  std::optional<std::string> comment;
};

struct CommentRequest {
  std::string text;
};

userver::formats::json::Value ToJson(const Assignment &assignment);
userver::formats::json::Value ToJson(const Submission &submission);
userver::formats::json::Value ToJson(const Comment &comment);
userver::formats::json::Value ToJson(const AssignmentDetail &detail);

} // namespace tutorflow::assignment
