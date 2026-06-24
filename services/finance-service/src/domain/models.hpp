#pragma once

#include <optional>
#include <string>

#include <userver/formats/json/value.hpp>

namespace tutorflow::finance {

struct Transaction {
  std::string id;
  std::string teacher_id;
  std::string student_id;
  std::string type;
  double amount{};
  std::string currency;
  std::optional<std::string> lesson_id;
  std::optional<std::string> receipt_id;
  std::optional<std::string> comment;
  std::string created_at;
};

struct Receipt {
  std::string id;
  std::string teacher_id;
  std::string student_id;
  std::string file_id;
  double amount{};
  std::string currency;
  std::string status;
  std::string submitted_at;
  std::optional<std::string> reviewed_at;
  std::optional<std::string> reviewed_by;
  std::optional<std::string> comment;
};

struct Balance {
  std::string student_id;
  std::string currency{"RUB"};
  double balance{};
};

struct CreateChargeRequest {
  std::string teacher_id;
  std::string student_id;
  std::string lesson_id;
  double amount{};
  std::string currency{"RUB"};
  std::optional<std::string> comment;
};

struct CreateReceiptRequest {
  std::string teacher_id;
  std::string student_id;
  std::string file_id;
  double amount{};
  std::string currency{"RUB"};
  std::optional<std::string> comment;
};

struct RejectReceiptRequest {
  std::optional<std::string> comment;
};

struct CreateChargeResult {
  Transaction transaction;
  bool created{};
};

userver::formats::json::Value ToJson(const Transaction &transaction);

} // namespace tutorflow::finance
