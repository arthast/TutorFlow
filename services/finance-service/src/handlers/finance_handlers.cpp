#include "handlers/finance_handlers.hpp"

#include <string>

#include <userver/components/component_context.hpp>
#include <userver/server/http/http_request.hpp>

#include <tutorflow/common/handler_helpers.hpp>

#include "domain/finance_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::finance {
namespace {
namespace http = userver::server::http;
using tutorflow::common::HandleEnvelope;
using tutorflow::common::JsonResponse;
using tutorflow::common::OptionalString;
using tutorflow::common::ParseJsonBody;
using tutorflow::common::RequireDouble;
using tutorflow::common::RequireString;

CreateChargeRequest ParseCreateChargeRequest(const http::HttpRequest &request) {
  const auto body = ParseJsonBody(request);
  return CreateChargeRequest{
      .teacher_id = RequireString(body, "teacher_id"),
      .student_id = RequireString(body, "student_id"),
      .lesson_id = RequireString(body, "lesson_id"),
      .amount = RequireDouble(body, "amount"),
      .currency = OptionalString(body, "currency").value_or("RUB"),
      .comment = OptionalString(body, "comment"),
  };
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

} // namespace tutorflow::finance
