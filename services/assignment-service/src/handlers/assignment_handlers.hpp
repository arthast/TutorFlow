#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::assignment {

class AssignmentService;

class CreateAssignmentHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-create-handler";

  CreateAssignmentHandler(const userver::components::ComponentConfig &config,
                          const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

class ListAssignmentsHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-list-handler";

  ListAssignmentsHandler(const userver::components::ComponentConfig &config,
                         const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

class GetAssignmentHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-get-handler";

  GetAssignmentHandler(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

class SubmitAssignmentHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-submit-handler";

  SubmitAssignmentHandler(const userver::components::ComponentConfig &config,
                          const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

class ReviewAssignmentHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-review-handler";

  ReviewAssignmentHandler(const userver::components::ComponentConfig &config,
                          const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

class CreateCommentHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "assignment-comment-handler";

  CreateCommentHandler(const userver::components::ComponentConfig &config,
                       const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const AssignmentService &service_;
};

} // namespace tutorflow::assignment
