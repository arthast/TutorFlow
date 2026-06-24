#include "clients/finance_grpc_client.hpp"

#include "clients/json_helpers.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include <userver/components/component_config.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/ugrpc/client/client_factory_component.hpp>
#include <userver/ugrpc/client/client_settings.hpp>
#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

namespace tutorflow::gateway {
namespace {
namespace common_formats = userver::formats::common;
namespace json = userver::formats::json;
namespace proto = tutorflow::finance::v1;
namespace common_proto = tutorflow::common::v1;

constexpr std::string_view kDefaultCurrency = "RUB";

json::Value ToJson(const proto::Transaction &transaction) {
  json::ValueBuilder body;
  body["id"] = transaction.id();
  body["teacher_id"] = transaction.teacher_id();
  body["student_id"] = transaction.student_id();
  body["type"] = transaction.type();
  body["amount"] = transaction.amount();
  body["currency"] = transaction.currency();
  body["lesson_id"] =
      NullableString(transaction.has_lesson_id(), transaction.lesson_id());
  body["receipt_id"] =
      NullableString(transaction.has_receipt_id(), transaction.receipt_id());
  body["comment"] =
      NullableString(transaction.has_comment(), transaction.comment());
  body["created_at"] = transaction.created_at();
  return body.ExtractValue();
}

json::Value ToJson(const proto::Receipt &receipt) {
  json::ValueBuilder body;
  body["id"] = receipt.id();
  body["teacher_id"] = receipt.teacher_id();
  body["student_id"] = receipt.student_id();
  body["file_id"] = receipt.file_id();
  body["amount"] = receipt.amount();
  body["currency"] = receipt.currency();
  body["status"] = receipt.status();
  body["submitted_at"] = receipt.submitted_at();
  body["reviewed_at"] =
      NullableString(receipt.has_reviewed_at(), receipt.reviewed_at());
  body["reviewed_by"] =
      NullableString(receipt.has_reviewed_by(), receipt.reviewed_by());
  body["comment"] = NullableString(receipt.has_comment(), receipt.comment());
  return body.ExtractValue();
}

json::Value ToJson(const proto::Balance &balance) {
  json::ValueBuilder body;
  body["student_id"] = balance.student_id();
  body["currency"] = balance.currency();
  body["balance"] = balance.balance();
  return body.ExtractValue();
}

} // namespace

GrpcFinanceClient::GrpcFinanceClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context),
      client_(context
                  .FindComponent<userver::ugrpc::client::ClientFactoryComponent>()
                  .GetFactory()
                  .MakeClient<proto::FinanceServiceClient>(
                      userver::ugrpc::client::ClientSettings{
                          .client_name = std::string{kName},
                          .endpoint = config["endpoint"].As<std::string>(),
                      })),
      options_{
          .timeout =
              std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)},
      } {}

json::Value GrpcFinanceClient::GetBalance(
    std::string_view student_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::GetBalanceRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_student_id(std::string{student_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.GetBalance(request,
                              tutorflow::clients::IdempotentCall(call_context, options_));
  }));
}

json::Value GrpcFinanceClient::ListTransactions(
    std::string_view student_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ListTransactionsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_student_id(std::string{student_id});
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListTransactions(request,
                                    tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &transaction : response.transactions()) {
    array.PushBack(ToJson(transaction));
  }
  return array.ExtractValue();
}

json::Value GrpcFinanceClient::ListReceipts(
    const std::optional<std::string> &status,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ListReceiptsRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  if (status) {
    request.set_status(*status);
  }
  const auto response = tutorflow::clients::InvokeUnary([&] {
    return client_.ListPaymentReceipts(
        request, tutorflow::clients::IdempotentCall(call_context, options_));
  });
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &receipt : response.receipts()) {
    array.PushBack(ToJson(receipt));
  }
  return array.ExtractValue();
}

json::Value GrpcFinanceClient::CreateReceipt(
    const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::CreateReceiptRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_teacher_id(tutorflow::common::RequireString(body, "teacher_id"));
  request.set_file_id(tutorflow::common::RequireString(body, "file_id"));
  request.set_amount(tutorflow::common::RequireDouble(body, "amount"));
  request.set_currency(tutorflow::common::OptionalString(body, "currency")
                           .value_or(std::string{kDefaultCurrency}));
  if (const auto comment = tutorflow::common::OptionalString(body, "comment")) {
    request.set_comment(*comment);
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.CreatePaymentReceipt(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcFinanceClient::ConfirmReceipt(
    std::string_view receipt_id,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::ConfirmReceiptRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_receipt_id(std::string{receipt_id});
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.ConfirmPaymentReceipt(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

json::Value GrpcFinanceClient::RejectReceipt(
    std::string_view receipt_id, const json::Value &body,
    const tutorflow::clients::GrpcCallContext &call_context) const {
  proto::RejectReceiptRequest request;
  tutorflow::clients::FillUserContext(*request.mutable_user(), call_context);
  request.set_receipt_id(std::string{receipt_id});
  if (const auto comment = tutorflow::common::OptionalString(body, "comment")) {
    request.set_comment(*comment);
  }
  return ToJson(tutorflow::clients::InvokeUnary([&] {
    return client_.RejectPaymentReceipt(
        request, tutorflow::clients::NonIdempotentCall(call_context, options_));
  }));
}

userver::yaml_config::Schema GrpcFinanceClient::GetStaticConfigSchema() {
  return userver::yaml_config::MergeSchemas<
      userver::components::LoggableComponentBase>(R"(
type: object
description: gateway finance gRPC client
additionalProperties: false
properties:
    endpoint:
        type: string
        description: finance gRPC endpoint
    timeout-ms:
        type: integer
        description: request timeout in milliseconds
        defaultDescription: '5000'
)");
}

} // namespace tutorflow::gateway
