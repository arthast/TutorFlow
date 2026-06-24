#include "grpc/finance_grpc_service.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>

#include <tutorflow/clients/grpc_server_utils.hpp>

#include "domain/models.hpp"

namespace tutorflow::finance {
namespace {
namespace proto = tutorflow::finance::v1;

using tutorflow::clients::InvokeServerUnary;
using tutorflow::clients::ResolveServerAuthContext;

constexpr std::string_view kDefaultCurrency = "RUB";

std::string CurrencyOrDefault(const std::string &currency) {
  return currency.empty() ? std::string{kDefaultCurrency} : currency;
}

proto::Transaction ToProto(const Transaction &transaction) {
  proto::Transaction response;
  response.set_id(transaction.id);
  response.set_teacher_id(transaction.teacher_id);
  response.set_student_id(transaction.student_id);
  response.set_type(transaction.type);
  response.set_amount(transaction.amount);
  response.set_currency(transaction.currency);
  if (transaction.lesson_id) {
    response.set_lesson_id(*transaction.lesson_id);
  }
  if (transaction.receipt_id) {
    response.set_receipt_id(*transaction.receipt_id);
  }
  if (transaction.comment) {
    response.set_comment(*transaction.comment);
  }
  response.set_created_at(transaction.created_at);
  return response;
}

proto::Receipt ToProto(const Receipt &receipt) {
  proto::Receipt response;
  response.set_id(receipt.id);
  response.set_teacher_id(receipt.teacher_id);
  response.set_student_id(receipt.student_id);
  response.set_file_id(receipt.file_id);
  response.set_amount(receipt.amount);
  response.set_currency(receipt.currency);
  response.set_status(receipt.status);
  response.set_submitted_at(receipt.submitted_at);
  if (receipt.reviewed_at) {
    response.set_reviewed_at(*receipt.reviewed_at);
  }
  if (receipt.reviewed_by) {
    response.set_reviewed_by(*receipt.reviewed_by);
  }
  if (receipt.comment) {
    response.set_comment(*receipt.comment);
  }
  return response;
}

proto::Balance ToProto(const Balance &balance) {
  proto::Balance response;
  response.set_student_id(balance.student_id);
  response.set_currency(balance.currency);
  response.set_balance(balance.balance);
  return response;
}

CreateChargeRequest FromProto(const proto::CreateChargeRequest &request) {
  return CreateChargeRequest{
      .teacher_id = request.teacher_id(),
      .student_id = request.student_id(),
      .lesson_id = request.lesson_id(),
      .amount = request.amount(),
      .currency = CurrencyOrDefault(request.currency()),
      .comment = request.has_comment()
                     ? std::optional<std::string>{request.comment()}
                     : std::nullopt,
  };
}

CreateReceiptRequest FromProto(const proto::CreateReceiptRequest &request,
                               const std::string &student_id) {
  return CreateReceiptRequest{
      .teacher_id = request.teacher_id(),
      .student_id = student_id,
      .file_id = request.file_id(),
      .amount = request.amount(),
      .currency = CurrencyOrDefault(request.currency()),
      .comment = request.has_comment()
                     ? std::optional<std::string>{request.comment()}
                     : std::nullopt,
  };
}

} // namespace

FinanceGrpcService::FinanceGrpcService(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : proto::FinanceServiceBase::Component(config, context),
      service_(context.FindComponent<FinanceService>()) {}

FinanceGrpcService::CreateChargeResult
FinanceGrpcService::CreateCharge(CallContext &,
                                 proto::CreateChargeRequest &&request) {
  return InvokeServerUnary<proto::Transaction>([&] {
    return ToProto(service_.CreateCharge(FromProto(request)).transaction);
  });
}

FinanceGrpcService::GetBalanceResult
FinanceGrpcService::GetBalance(CallContext &context,
                               proto::GetBalanceRequest &&request) {
  return InvokeServerUnary<proto::Balance>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.GetBalance(auth, request.student_id()));
  });
}

FinanceGrpcService::ListTransactionsResult
FinanceGrpcService::ListTransactions(CallContext &context,
                                     proto::ListTransactionsRequest &&request) {
  return InvokeServerUnary<proto::ListTransactionsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    proto::ListTransactionsResponse response;
    for (const auto &transaction :
         service_.ListTransactions(auth, request.student_id())) {
      *response.add_transactions() = ToProto(transaction);
    }
    return response;
  });
}

FinanceGrpcService::CreatePaymentReceiptResult
FinanceGrpcService::CreatePaymentReceipt(CallContext &context,
                                         proto::CreateReceiptRequest &&request) {
  return InvokeServerUnary<proto::Receipt>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(
        service_.CreateReceipt(auth, FromProto(request, auth.user_id)));
  });
}

FinanceGrpcService::ListPaymentReceiptsResult
FinanceGrpcService::ListPaymentReceipts(CallContext &context,
                                        proto::ListReceiptsRequest &&request) {
  return InvokeServerUnary<proto::ListReceiptsResponse>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    const auto status =
        request.has_status() ? std::optional<std::string>{request.status()}
                             : std::nullopt;
    proto::ListReceiptsResponse response;
    for (const auto &receipt : service_.ListReceipts(auth, status)) {
      *response.add_receipts() = ToProto(receipt);
    }
    return response;
  });
}

FinanceGrpcService::ConfirmPaymentReceiptResult
FinanceGrpcService::ConfirmPaymentReceipt(
    CallContext &context, proto::ConfirmReceiptRequest &&request) {
  return InvokeServerUnary<proto::Receipt>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    return ToProto(service_.ConfirmReceipt(auth, request.receipt_id()));
  });
}

FinanceGrpcService::RejectPaymentReceiptResult
FinanceGrpcService::RejectPaymentReceipt(CallContext &context,
                                         proto::RejectReceiptRequest &&request) {
  return InvokeServerUnary<proto::Receipt>([&] {
    const auto auth = ResolveServerAuthContext(context, request.user());
    RejectReceiptRequest reject{
        .comment = request.has_comment()
                       ? std::optional<std::string>{request.comment()}
                       : std::nullopt,
    };
    return ToProto(service_.RejectReceipt(auth, request.receipt_id(), reject));
  });
}

} // namespace tutorflow::finance
