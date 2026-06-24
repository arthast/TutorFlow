#pragma once

#include <tutorflow/finance_service.usrv.pb.hpp>

#include "domain/finance_service.hpp"

namespace tutorflow::finance {

class FinanceGrpcService final
    : public tutorflow::finance::v1::FinanceServiceBase::Component {
public:
  static constexpr std::string_view kName = "finance-grpc-service";

  FinanceGrpcService(const userver::components::ComponentConfig &config,
                     const userver::components::ComponentContext &context);

  CreateChargeResult
  CreateCharge(CallContext &context,
               tutorflow::finance::v1::CreateChargeRequest &&request) override;
  GetBalanceResult
  GetBalance(CallContext &context,
             tutorflow::finance::v1::GetBalanceRequest &&request) override;
  ListTransactionsResult ListTransactions(
      CallContext &context,
      tutorflow::finance::v1::ListTransactionsRequest &&request) override;
  CreatePaymentReceiptResult CreatePaymentReceipt(
      CallContext &context,
      tutorflow::finance::v1::CreateReceiptRequest &&request) override;
  ListPaymentReceiptsResult ListPaymentReceipts(
      CallContext &context,
      tutorflow::finance::v1::ListReceiptsRequest &&request) override;
  ConfirmPaymentReceiptResult ConfirmPaymentReceipt(
      CallContext &context,
      tutorflow::finance::v1::ConfirmReceiptRequest &&request) override;
  RejectPaymentReceiptResult RejectPaymentReceipt(
      CallContext &context,
      tutorflow::finance::v1::RejectReceiptRequest &&request) override;

private:
  FinanceService &service_;
};

} // namespace tutorflow::finance
