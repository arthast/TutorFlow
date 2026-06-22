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
  Balance GetBalance(const std::string &student_id) const;
  std::vector<Transaction>
  ListTransactions(const std::string &student_id) const;

  Receipt CreateReceipt(const CreateReceiptRequest &request) const;
  std::vector<Receipt>
  ListReceipts(const std::string &teacher_id,
               const std::optional<std::string> &status) const;
  std::optional<Receipt> FindReceipt(const std::string &receipt_id) const;
  Receipt ConfirmReceipt(const std::string &receipt_id,
                         const std::string &teacher_id) const;
  Receipt RejectReceipt(const std::string &receipt_id,
                        const std::string &teacher_id,
                        const std::optional<std::string> &comment) const;

private:
  userver::storages::postgres::ClusterPtr pg_;
};

} // namespace tutorflow::finance
