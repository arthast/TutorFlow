#include "handlers/finance_handlers.hpp"

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/handler_helpers.hpp>

#include "domain/finance_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::finance {
namespace {
namespace http = userver::server::http;
namespace json = userver::formats::json;
namespace common_formats = userver::formats::common;
using tutorflow::common::HandleEnvelope;
using tutorflow::common::JsonResponse;
using tutorflow::common::OptionalString;
using tutorflow::common::ParseJsonBody;
using tutorflow::common::RequireDouble;
using tutorflow::common::RequireString;
using tutorflow::common::ServiceError;

std::string OptionalCurrency(const json::Value &body) {
  return OptionalString(body, "currency").value_or("RUB");
}

CreateChargeRequest ParseCreateChargeRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CreateChargeRequest{
      .teacher_id = RequireString(body, "teacher_id"),
      .student_id = RequireString(body, "student_id"),
      .lesson_id = RequireString(body, "lesson_id"),
      .amount = RequireDouble(body, "amount"),
      .currency = OptionalCurrency(body),
      .comment = OptionalString(body, "comment"),
  };
}

CreateReceiptRequest ParseCreateReceiptRequest(
    const http::HttpRequest &request, const std::string &student_id) {
  const auto body = ParseJsonBody(request);
  return CreateReceiptRequest{
      .teacher_id = RequireString(body, "teacher_id"),
      .student_id = student_id,
      .file_id = RequireString(body, "file_id"),
      .amount = RequireDouble(body, "amount"),
      .currency = OptionalCurrency(body),
      .comment = OptionalString(body, "comment"),
  };
}

RejectReceiptRequest
ParseRejectReceiptRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request, /*allow_empty=*/true);
  return RejectReceiptRequest{.comment = OptionalString(body, "comment")};
}

std::string RequiredPathArg(const http::HttpRequest &request,
                            std::string_view name) {
  auto value = request.GetPathArg(name);
  if (value.empty()) {
    throw ServiceError::Validation("missing " + std::string{name} +
                                   " path parameter");
  }
  return value;
}

std::optional<std::string> OptionalQueryArg(const http::HttpRequest &request,
                                            std::string_view name) {
  const auto &value = request.GetArg(name);
  if (value.empty())
    return std::nullopt;
  return value;
}

template <typename T> json::Value ToJsonArray(const std::vector<T> &items) {
  json::ValueBuilder array(common_formats::Type::kArray);
  for (const auto &item : items) {
    array.PushBack(ToJson(item));
  }
  return array.ExtractValue();
}

} // namespace

CreateChargeHandler::CreateChargeHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string CreateChargeHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto result =
        service_.CreateCharge(ParseCreateChargeRequest(request));
    return JsonResponse(request, ToJson(result.transaction),
                        result.created ? http::HttpStatus::kCreated
                                       : http::HttpStatus::kOk);
  });
}

GetBalanceHandler::GetBalanceHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string GetBalanceHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request, ToJson(service_.GetBalance(
                     auth, RequiredPathArg(request, "studentId"))));
  });
}

ListTransactionsHandler::ListTransactionsHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string ListTransactionsHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(
        request, ToJsonArray(service_.ListTransactions(
                     auth, RequiredPathArg(request, "studentId"))));
  });
}

CreateReceiptHandler::CreateReceiptHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string CreateReceiptHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJson(service_.CreateReceipt(
                            auth,
                            ParseCreateReceiptRequest(request, auth.user_id))),
                        http::HttpStatus::kCreated);
  });
}

ListReceiptsHandler::ListReceiptsHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string ListReceiptsHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJsonArray(service_.ListReceipts(
                            auth, OptionalQueryArg(request, "status"))));
  });
}

ConfirmReceiptHandler::ConfirmReceiptHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string ConfirmReceiptHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJson(service_.ConfirmReceipt(
                            auth, RequiredPathArg(request, "receiptId"))));
  });
}

RejectReceiptHandler::RejectReceiptHandler(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FinanceService>()) {}

std::string RejectReceiptHandler::HandleRequestThrow(
    const http::HttpRequest &request,
    userver::server::request::RequestContext &) const {
  return HandleEnvelope(request, [&] {
    const auto auth = tutorflow::common::ParseAuthContext(request);
    return JsonResponse(request,
                        ToJson(service_.RejectReceipt(
                            auth, RequiredPathArg(request, "receiptId"),
                            ParseRejectReceiptRequest(request))));
  });
}

} // namespace tutorflow::finance
