#include "repositories/finance_repository.hpp"

#include <optional>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/result_set.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::finance {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave = pg::ClusterHostType::kSlave;

constexpr std::string_view kTransactionFields = R"(
  id::text,
  teacher_id::text,
  student_id::text,
  type,
  amount::double precision AS amount,
  currency,
  COALESCE(lesson_id::text, '') AS lesson_id,
  COALESCE(receipt_id::text, '') AS receipt_id,
  COALESCE(comment, '') AS comment,
  to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

constexpr std::string_view kReceiptFields = R"(
  id::text,
  teacher_id::text,
  student_id::text,
  file_id::text,
  amount::double precision AS amount,
  currency,
  status,
  to_char(submitted_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS submitted_at,
  COALESCE(to_char(reviewed_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"'), '') AS reviewed_at,
  COALESCE(reviewed_by::text, '') AS reviewed_by,
  COALESCE(comment, '') AS comment
)";

std::optional<std::string> EmptyToNull(std::string value) {
  if (value.empty())
    return std::nullopt;
  return value;
}

std::string BalanceBeforeExpr(std::string_view row_alias) {
  return "SELECT COALESCE(SUM(CASE type "
         "  WHEN 'charge' THEN amount "
         "  WHEN 'payment' THEN -amount "
         "  WHEN 'correction' THEN amount "
         "  WHEN 'refund' THEN -amount "
         "  ELSE 0 END), 0) "
         "FROM financial_transactions ft "
         "WHERE ft.student_id = " +
         std::string{row_alias} +
         ".student_id::uuid AND ft.currency = " + std::string{row_alias} +
         ".currency";
}

Transaction RowToTransaction(const pg::Row &row) {
  return Transaction{
      row["id"].As<std::string>(),
      row["teacher_id"].As<std::string>(),
      row["student_id"].As<std::string>(),
      row["type"].As<std::string>(),
      row["amount"].As<double>(),
      row["currency"].As<std::string>(),
      EmptyToNull(row["lesson_id"].As<std::string>()),
      EmptyToNull(row["receipt_id"].As<std::string>()),
      EmptyToNull(row["comment"].As<std::string>()),
      row["created_at"].As<std::string>(),
  };
}

Receipt RowToReceipt(const pg::Row &row) {
  return Receipt{
      row["id"].As<std::string>(),
      row["teacher_id"].As<std::string>(),
      row["student_id"].As<std::string>(),
      row["file_id"].As<std::string>(),
      row["amount"].As<double>(),
      row["currency"].As<std::string>(),
      row["status"].As<std::string>(),
      row["submitted_at"].As<std::string>(),
      EmptyToNull(row["reviewed_at"].As<std::string>()),
      EmptyToNull(row["reviewed_by"].As<std::string>()),
      EmptyToNull(row["comment"].As<std::string>()),
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

Receipt RequireSingleReceipt(const pg::ResultSet &result) {
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::NotFound("receipt not found");
  }
  return RowToReceipt(result[0]);
}

} // namespace

FinanceRepository::FinanceRepository(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("finance-db")
              .GetCluster()) {}

CreateChargeResult
FinanceRepository::CreateCharge(const CreateChargeRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH inserted AS ("
      "  INSERT INTO financial_transactions "
      "    (teacher_id, student_id, type, amount, currency, lesson_id, "
      "comment) "
      "  VALUES ($1::uuid, $2::uuid, 'charge', $4::numeric, $5, $3::uuid, $6) "
          "  ON CONFLICT (lesson_id) WHERE type = 'charge' DO NOTHING "
          "  RETURNING " +
          std::string{kTransactionFields} +
          ", TRUE AS created"
          "), charge_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'transaction', id::uuid, 'charge.created', 1, "
          "         jsonb_build_object("
          "           'transaction_id', id, "
          "           'teacher_id', teacher_id, "
          "           'student_id', student_id, "
          "           'lesson_id', lesson_id, "
          "           'amount', amount, "
          "           'currency', currency, "
          "           'created_at', created_at) "
          "  FROM inserted"
          "), balance_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'student_balance', student_id::uuid, 'balance.changed', 1, "
          "         jsonb_build_object("
          "           'student_id', student_id, "
          "           'teacher_id', teacher_id, "
          "           'delta', amount, "
          "           'balance_amount', ((" +
          BalanceBeforeExpr("inserted") +
          ") + inserted.amount::numeric)::double precision, "
          "           'currency', currency, "
          "           'reason', 'charge.created', "
          "           'source_transaction_id', id, "
          "           'changed_at', created_at) "
          "  FROM inserted"
          "), existing AS ("
          "  SELECT " +
          std::string{kTransactionFields} +
          ", FALSE AS created "
          "  FROM financial_transactions "
          "  WHERE type = 'charge' AND lesson_id = $3::uuid"
          ") SELECT * FROM inserted "
          "UNION ALL "
          "SELECT * FROM existing WHERE NOT EXISTS (SELECT 1 FROM inserted) "
          "LIMIT 1",
      request.teacher_id, request.student_id, request.lesson_id, request.amount,
      request.currency, request.comment);

  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Internal("charge was not created");
  }
  return {.transaction = RowToTransaction(result[0]),
          .created = result[0]["created"].As<bool>()};
}

Transaction
FinanceRepository::CreateCorrection(const CreateCorrectionRequest &request) const {
  // Ручная коррекция: append-only строка correction (без lesson_id) + событие
  // balance.changed в одной транзакции (CTE).
  const auto result = pg_->Execute(
      kMaster,
      "WITH inserted AS ("
      "  INSERT INTO financial_transactions "
      "    (teacher_id, student_id, type, amount, currency, comment) "
      "  VALUES ($1::uuid, $2::uuid, 'correction', $3::numeric, $4, $5) "
      "  RETURNING " +
          std::string{kTransactionFields} +
          "), balance_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'student_balance', student_id::uuid, 'balance.changed', 1, "
          "         jsonb_build_object("
          "           'student_id', student_id, "
          "           'teacher_id', teacher_id, "
          "           'delta', amount, "
          "           'balance_amount', ((" +
          BalanceBeforeExpr("inserted") +
          ") + inserted.amount::numeric)::double precision, "
          "           'currency', currency, "
          "           'reason', 'correction.created', "
          "           'source_transaction_id', id, "
          "           'changed_at', created_at) "
          "  FROM inserted"
          ") SELECT * FROM inserted",
      request.teacher_id, request.student_id, request.amount, request.currency,
      request.comment);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Internal("correction was not created");
  }
  return RowToTransaction(result[0]);
}

bool FinanceRepository::CreateEventCorrection(
    const CreateCorrectionRequest &request, const std::string &event_id,
    const std::string &event_type) const {
  // Компенсация/восстановление из lesson lifecycle: processed_events,
  // correction и balance.changed пишутся одним statement. Если event_id уже был
  // принят, accepted_event пустой -> correction/outbox не создаются.
  const auto lesson_id = request.lesson_id.value_or("");
  const auto result = pg_->Execute(
      kMaster,
      "WITH accepted_event AS ("
      "  INSERT INTO processed_events (event_id, event_type) "
      "  VALUES ($7::uuid, $8) "
      "  ON CONFLICT (event_id) DO NOTHING "
      "  RETURNING event_id"
      "), inserted AS ("
      "  INSERT INTO financial_transactions "
      "    (teacher_id, student_id, type, amount, currency, lesson_id, comment) "
      "  SELECT $1::uuid, $2::uuid, 'correction', $3::numeric, $4, "
      "         NULLIF($5, '')::uuid, $6 "
      "  FROM accepted_event "
      "  RETURNING " +
          std::string{kTransactionFields} +
          "), balance_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'student_balance', student_id::uuid, 'balance.changed', 1, "
          "         jsonb_build_object("
          "           'student_id', student_id, "
          "           'teacher_id', teacher_id, "
          "           'delta', amount, "
          "           'balance_amount', ((" +
          BalanceBeforeExpr("inserted") +
          ") + inserted.amount::numeric)::double precision, "
          "           'currency', currency, "
          "           'reason', 'correction.created', "
          "           'source_transaction_id', id, "
          "           'changed_at', created_at) "
          "  FROM inserted"
          ") SELECT EXISTS(SELECT 1 FROM inserted) AS created",
      request.teacher_id, request.student_id, request.amount, request.currency,
      lesson_id, request.comment, event_id, event_type);
  if (result.IsEmpty()) {
    throw tutorflow::common::ServiceError::Internal(
        "event correction result is missing");
  }
  return result[0]["created"].As<bool>();
}

Balance FinanceRepository::GetBalance(const std::string &student_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT COALESCE(SUM(CASE type "
                   "  WHEN 'charge' THEN amount "
                   "  WHEN 'payment' THEN -amount "
                   "  WHEN 'correction' THEN amount "
                   "  WHEN 'refund' THEN -amount "
                   "  ELSE 0 END), 0)::double precision AS balance "
                   "FROM financial_transactions "
                   "WHERE student_id = $1::uuid AND currency = 'RUB'",
                   student_id);
  return Balance{.student_id = student_id,
                 .currency = "RUB",
                 .balance = result[0]["balance"].As<double>()};
}

std::vector<Transaction>
FinanceRepository::ListTransactions(const std::string &student_id) const {
  const auto result = pg_->Execute(kSlave,
                                   "SELECT " + std::string{kTransactionFields} +
                                       " FROM financial_transactions "
                                       "WHERE student_id = $1::uuid "
                                       "ORDER BY created_at DESC, id DESC",
                                   student_id);
  return RowsToVector<Transaction>(result, RowToTransaction);
}

Receipt
FinanceRepository::CreateReceipt(const CreateReceiptRequest &request) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH inserted AS ("
      "  INSERT INTO payment_receipts "
      "    (teacher_id, student_id, file_id, amount, currency, comment) "
      "  VALUES ($1::uuid, $2::uuid, $3::uuid, $4::numeric, $5, $6) "
      "  RETURNING *"
      "), outbox AS ("
      "  INSERT INTO outbox_events "
      "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
      "  SELECT 'payment_receipt', id, 'payment_receipt.uploaded', 1, "
      "         jsonb_build_object("
      "           'receipt_id', id::text, "
      "           'teacher_id', teacher_id::text, "
      "           'student_id', student_id::text, "
      "           'file_id', file_id::text, "
      "           'amount', amount::double precision, "
      "           'currency', currency, "
      "           'status', status, "
      "           'submitted_at', to_char(submitted_at AT TIME ZONE 'UTC', "
      "                                  'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')) "
      "  FROM inserted"
      ") SELECT " +
          std::string{kReceiptFields} +
      " FROM inserted",
      request.teacher_id, request.student_id, request.file_id, request.amount,
      request.currency, request.comment);
  return RequireSingleReceipt(result);
}

std::vector<Receipt> FinanceRepository::ListReceiptsForTeacher(
    const std::string &teacher_id,
    const std::optional<std::string> &status) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT " + std::string{kReceiptFields} +
          " FROM payment_receipts "
          "WHERE teacher_id = $1::uuid AND ($2 = '' OR status = $2) "
          "ORDER BY submitted_at DESC, id DESC",
      teacher_id, status.value_or(""));
  return RowsToVector<Receipt>(result, RowToReceipt);
}

std::vector<Receipt> FinanceRepository::ListReceiptsForStudent(
    const std::string &student_id,
    const std::optional<std::string> &status) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT " + std::string{kReceiptFields} +
          " FROM payment_receipts "
          "WHERE student_id = $1::uuid AND ($2 = '' OR status = $2) "
          "ORDER BY submitted_at DESC, id DESC",
      student_id, status.value_or(""));
  return RowsToVector<Receipt>(result, RowToReceipt);
}

std::optional<Receipt>
FinanceRepository::FindReceipt(const std::string &receipt_id) const {
  const auto result =
      pg_->Execute(kSlave,
                   "SELECT " + std::string{kReceiptFields} +
                       " FROM payment_receipts WHERE id = $1::uuid",
                   receipt_id);
  if (result.IsEmpty())
    return std::nullopt;
  return RowToReceipt(result[0]);
}

Receipt FinanceRepository::ConfirmReceipt(const std::string &receipt_id,
                                          const std::string &teacher_id) const {
  const auto result = pg_->Execute(
      kMaster,
      "WITH updated AS ("
      "  UPDATE payment_receipts "
      "  SET status = 'confirmed', "
      "      reviewed_at = COALESCE(reviewed_at, now()), "
      "      reviewed_by = COALESCE(reviewed_by, $2::uuid) "
      "  WHERE id = $1::uuid AND teacher_id = $2::uuid "
      "    AND status IN ('pending_review', 'confirmed') "
      "  RETURNING " +
          std::string{kReceiptFields} +
          "), payment AS ("
          "  INSERT INTO financial_transactions "
          "    (teacher_id, student_id, type, amount, currency, receipt_id, "
          "comment) "
          "  SELECT teacher_id::uuid, student_id::uuid, 'payment', "
          "         amount::numeric, currency, id::uuid, "
          "         'Payment receipt confirmed' "
          "  FROM updated "
          "  ON CONFLICT (receipt_id) WHERE type = 'payment' DO NOTHING "
          "  RETURNING " +
          std::string{kTransactionFields} +
          "), payment_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'payment_receipt', receipt_id::uuid, 'payment.confirmed', 1, "
          "         jsonb_build_object("
          "           'receipt_id', receipt_id, "
          "           'payment_id', id, "
          "           'teacher_id', teacher_id, "
          "           'student_id', student_id, "
          "           'amount', amount, "
          "           'currency', currency, "
          "           'confirmed_at', created_at) "
          "  FROM payment"
          "), balance_outbox AS ("
          "  INSERT INTO outbox_events "
          "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
          "  SELECT 'student_balance', student_id::uuid, 'balance.changed', 1, "
          "         jsonb_build_object("
          "           'student_id', student_id, "
          "           'teacher_id', teacher_id, "
          "           'delta', -amount, "
          "           'balance_amount', ((" +
          BalanceBeforeExpr("payment") +
          ") - payment.amount::numeric)::double precision, "
          "           'currency', currency, "
          "           'reason', 'payment.confirmed', "
          "           'source_transaction_id', id, "
          "           'changed_at', created_at) "
          "  FROM payment"
          ") SELECT * FROM updated",
      receipt_id, teacher_id);
  return RequireSingleReceipt(result);
}

Receipt FinanceRepository::RejectReceipt(
    const std::string &receipt_id, const std::string &teacher_id,
    const std::optional<std::string> &comment) const {
  const auto result =
      pg_->Execute(kMaster,
                   "WITH updated AS ("
                   "  UPDATE payment_receipts "
                   "  SET status = 'rejected', "
                   "      reviewed_at = COALESCE(reviewed_at, now()), "
                   "      reviewed_by = COALESCE(reviewed_by, $2::uuid), "
                   "      comment = COALESCE($3, comment) "
                   "  WHERE id = $1::uuid AND teacher_id = $2::uuid "
                   "    AND status IN ('pending_review', 'rejected') "
                   "  RETURNING *"
                   "), outbox AS ("
                   "  INSERT INTO outbox_events "
                   "    (aggregate_type, aggregate_id, event_type, event_version, payload) "
                   "  SELECT 'payment_receipt', id, 'payment.rejected', 1, "
                   "         jsonb_build_object("
                   "           'receipt_id', id::text, "
                   "           'teacher_id', teacher_id::text, "
                   "           'student_id', student_id::text, "
                   "           'amount', amount::double precision, "
                   "           'currency', currency, "
                   "           'comment', comment, "
                   "           'rejected_at', to_char(reviewed_at AT TIME ZONE 'UTC', "
                   "                                  'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')) "
                   "  FROM updated"
                   ") SELECT " +
                       std::string{kReceiptFields} +
                   " FROM updated",
                   receipt_id, teacher_id, comment);
  return RequireSingleReceipt(result);
}

bool FinanceRepository::IsEventProcessed(const std::string &event_id) const {
  const auto result = pg_->Execute(
      kSlave,
      "SELECT 1 FROM processed_events WHERE event_id = $1::uuid LIMIT 1",
      event_id);
  return !result.IsEmpty();
}

void FinanceRepository::MarkEventProcessed(const std::string &event_id,
                                           const std::string &event_type) const {
  pg_->Execute(kMaster,
               "INSERT INTO processed_events (event_id, event_type) "
               "VALUES ($1::uuid, $2) ON CONFLICT DO NOTHING",
               event_id, event_type);
}

} // namespace tutorflow::finance
