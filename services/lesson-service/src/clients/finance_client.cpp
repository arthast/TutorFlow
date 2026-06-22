#include "clients/finance_client.hpp"

#include <userver/logging/log.hpp>

namespace tutorflow::lesson {

StubFinanceClient::StubFinanceClient(
    const userver::components::ComponentConfig &config,
    const userver::components::ComponentContext &context)
    : LoggableComponentBase(config, context) {}

void StubFinanceClient::CreateCharge(const ChargeRequest &request) const {
  LOG_INFO() << "Stub finance create charge lesson_id=" << request.lesson_id
             << " teacher_id=" << request.teacher_id
             << " student_id=" << request.student_id
             << " amount=" << request.amount
             << " currency=" << request.currency;
}

} // namespace tutorflow::lesson
