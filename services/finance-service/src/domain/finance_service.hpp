#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include "domain/models.hpp"

namespace tutorflow::common {
struct AuthContext;
}

namespace tutorflow::clients {
class IdentityClient;
}

namespace tutorflow::finance {

class FinanceRepository;

class FinanceService final : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "finance-domain-service";

  FinanceService(const userver::components::ComponentConfig &config,
                 const userver::components::ComponentContext &context);

  CreateChargeResult CreateCharge(const CreateChargeRequest &request) const;

  // 5L.5: ручная коррекция баланса преподавателем. Доступ — teacher с активной
  // связью с учеником (check-access); amount=0 -> 422; comment обязателен.
  Transaction CreateCorrection(const tutorflow::common::AuthContext &auth,
                               const std::string &student_id, double amount,
                               const std::string &currency,
                               const std::string &comment) const;

  // 5L.4: компенсация отменённого завершённого занятия. Вызывается из консьюмера
  // lesson.cancelled (внутренний путь, без auth). Идемпотентно по lesson_id.
  CreateCorrectionResult
  CompensateCancelledLesson(const CreateCorrectionRequest &request) const;

  Balance GetBalance(const tutorflow::common::AuthContext &auth,
                     const std::string &student_id) const;
  std::vector<Transaction>
  ListTransactions(const tutorflow::common::AuthContext &auth,
                   const std::string &student_id) const;

  Receipt CreateReceipt(const tutorflow::common::AuthContext &auth,
                        const CreateReceiptRequest &request) const;
  std::vector<Receipt>
  ListReceipts(const tutorflow::common::AuthContext &auth,
               const std::optional<std::string> &status) const;
  Receipt ConfirmReceipt(const tutorflow::common::AuthContext &auth,
                         const std::string &receipt_id) const;
  Receipt RejectReceipt(const tutorflow::common::AuthContext &auth,
                        const std::string &receipt_id,
                        const RejectReceiptRequest &request) const;

private:
  // Доступ к учётным данным ученика: разрешён самому ученику или его
  // преподавателю (identity check-access); иначе ServiceError::Forbidden.
  void EnsureStudentAccess(const tutorflow::common::AuthContext &auth,
                           const std::string &student_id) const;

  FinanceRepository &repository_;
  tutorflow::clients::IdentityClient &identity_;
};

} // namespace tutorflow::finance
