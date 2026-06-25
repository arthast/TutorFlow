#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::finance {

class FinanceRepository final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "finance-repository";

  FinanceRepository(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  CreateChargeResult CreateCharge(const CreateChargeRequest &request) const;
  // Ручная коррекция (lesson_id пуст): всегда добавляет строку correction.
  Transaction CreateCorrection(const CreateCorrectionRequest &request) const;
  // Коррекция из доменного события lesson.*: идемпотентна атомарным inbox по
  // event_id. Дубль события -> false, без второй correction и balance.changed.
  bool CreateEventCorrection(const CreateCorrectionRequest &request,
                             const std::string &event_id,
                             const std::string &event_type) const;
  Balance GetBalance(const std::string &student_id) const;
  std::vector<Transaction>
  ListTransactions(const std::string &student_id) const;

  Receipt CreateReceipt(const CreateReceiptRequest &request) const;
  std::vector<Receipt>
  ListReceiptsForTeacher(const std::string &teacher_id,
                         const std::optional<std::string> &status) const;
  std::vector<Receipt>
  ListReceiptsForStudent(const std::string &student_id,
                         const std::optional<std::string> &status) const;
  std::optional<Receipt> FindReceipt(const std::string &receipt_id) const;
  Receipt ConfirmReceipt(const std::string &receipt_id,
                         const std::string &teacher_id) const;
  Receipt RejectReceipt(const std::string &receipt_id,
                        const std::string &teacher_id,
                        const std::optional<std::string> &comment) const;
  bool IsEventProcessed(const std::string &event_id) const;
  void MarkEventProcessed(const std::string &event_id,
                          const std::string &event_type) const;

private:
  userver::storages::postgres::ClusterPtr pg_;
};

} // namespace tutorflow::finance
