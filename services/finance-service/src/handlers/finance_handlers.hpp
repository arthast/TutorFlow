#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::finance {

class FinanceService;

class CreateChargeHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-create-charge-handler";

  CreateChargeHandler(const userver::components::ComponentConfig &config,
                      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class GetBalanceHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-get-balance-handler";

  GetBalanceHandler(const userver::components::ComponentConfig &config,
                    const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class ListTransactionsHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-list-transactions-handler";

  ListTransactionsHandler(const userver::components::ComponentConfig &config,
                          const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class CreateReceiptHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-create-receipt-handler";

  CreateReceiptHandler(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class ListReceiptsHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-list-receipts-handler";

  ListReceiptsHandler(const userver::components::ComponentConfig &config,
                      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class ConfirmReceiptHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-confirm-receipt-handler";

  ConfirmReceiptHandler(const userver::components::ComponentConfig &config,
                        const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

class RejectReceiptHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "finance-reject-receipt-handler";

  RejectReceiptHandler(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const FinanceService &service_;
};

} // namespace tutorflow::finance
