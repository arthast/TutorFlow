#include "domain/finance_service.hpp"

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/clients/identity_client.hpp>

#include "repositories/finance_repository.hpp"

namespace tutorflow::finance {
namespace {

void ValidateAmount(double amount) {
  if (amount <= 0.0) {
    throw tutorflow::common::ServiceError::Validation(
        "amount must be greater than zero");
  }
}

void ValidateCurrency(const std::string &currency) {
  if (currency.empty()) {
    throw tutorflow::common::ServiceError::Validation(
        "currency must not be empty");
  }
}

void ValidateStatusFilter(const std::optional<std::string> &status) {
  if (!status.has_value())
    return;
  if (*status == "pending_review" || *status == "confirmed" ||
      *status == "rejected") {
    return;
  }
  throw tutorflow::common::ServiceError::Validation("invalid receipt status");
}

} // namespace

FinanceService::FinanceService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<FinanceRepository>()),
      identity_(context.FindComponent<tutorflow::clients::HttpIdentityClient>()) {}

CreateChargeResult
FinanceService::CreateCharge(const CreateChargeRequest &request) const {
  ValidateAmount(request.amount);
  ValidateCurrency(request.currency);
  return repository_.CreateCharge(request);
}

void FinanceService::EnsureStudentAccess(
    const tutorflow::common::AuthContext &auth,
    const std::string &student_id) const {
  if (auth.user_id == student_id) {
    return;  // сам ученик
  }
  // Иначе зовущий должен быть преподавателем этого ученика.
  if (identity_.CheckAccess(auth.user_id, student_id).allowed) {
    return;
  }
  throw tutorflow::common::ServiceError::Forbidden(
      "not allowed to access this student's finance data");
}

Balance FinanceService::GetBalance(const tutorflow::common::AuthContext &auth,
                                   const std::string &student_id) const {
  EnsureStudentAccess(auth, student_id);
  return repository_.GetBalance(student_id);
}

std::vector<Transaction>
FinanceService::ListTransactions(const tutorflow::common::AuthContext &auth,
                                 const std::string &student_id) const {
  EnsureStudentAccess(auth, student_id);
  return repository_.ListTransactions(student_id);
}

Receipt
FinanceService::CreateReceipt(const tutorflow::common::AuthContext &auth,
                              const CreateReceiptRequest &request) const {
  if (auth.user_id != request.student_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "student can create only own receipt");
  }
  ValidateAmount(request.amount);
  ValidateCurrency(request.currency);

  const auto access =
      identity_.CheckAccess(request.teacher_id, request.student_id);
  if (!access.allowed || access.status != "active") {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher-student relation is not active");
  }

  return repository_.CreateReceipt(request);
}

std::vector<Receipt>
FinanceService::ListReceipts(const tutorflow::common::AuthContext &auth,
                             const std::optional<std::string> &status) const {
  ValidateStatusFilter(status);
  if (auth.IsTeacher()) {
    return repository_.ListReceiptsForTeacher(auth.user_id, status);
  }
  if (auth.IsStudent()) {
    return repository_.ListReceiptsForStudent(auth.user_id, status);
  }
  throw tutorflow::common::ServiceError::Forbidden(
      "teacher or student role required");
}

Receipt
FinanceService::ConfirmReceipt(const tutorflow::common::AuthContext &auth,
                               const std::string &receipt_id) const {
  const auto receipt = repository_.FindReceipt(receipt_id);
  if (!receipt.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("receipt not found");
  }
  if (receipt->teacher_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own receipt");
  }
  if (receipt->status == "rejected") {
    throw tutorflow::common::ServiceError::Conflict(
        "rejected receipt cannot be confirmed");
  }
  return repository_.ConfirmReceipt(receipt_id, auth.user_id);
}

Receipt
FinanceService::RejectReceipt(const tutorflow::common::AuthContext &auth,
                              const std::string &receipt_id,
                              const RejectReceiptRequest &request) const {
  const auto receipt = repository_.FindReceipt(receipt_id);
  if (!receipt.has_value()) {
    throw tutorflow::common::ServiceError::NotFound("receipt not found");
  }
  if (receipt->teacher_id != auth.user_id) {
    throw tutorflow::common::ServiceError::Forbidden(
        "teacher does not own receipt");
  }
  if (receipt->status == "confirmed") {
    throw tutorflow::common::ServiceError::Conflict(
        "confirmed receipt cannot be rejected");
  }
  if (receipt->status == "rejected")
    return *receipt;
  return repository_.RejectReceipt(receipt_id, auth.user_id, request.comment);
}

} // namespace tutorflow::finance
