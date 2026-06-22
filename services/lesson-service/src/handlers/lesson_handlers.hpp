#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::lesson {

class LessonService;

class CreateAvailabilityHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName =
      "lesson-create-availability-handler";
  CreateAvailabilityHandler(
      const userver::components::ComponentConfig &config,
      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class ListAvailabilityHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-list-availability-handler";
  ListAvailabilityHandler(const userver::components::ComponentConfig &config,
                          const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class CreateLessonHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-create-handler";
  CreateLessonHandler(const userver::components::ComponentConfig &config,
                      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class ListLessonsHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-list-handler";
  ListLessonsHandler(const userver::components::ComponentConfig &config,
                     const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class GetLessonHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-get-handler";
  GetLessonHandler(const userver::components::ComponentConfig &config,
                   const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class CompleteLessonHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-complete-handler";
  CompleteLessonHandler(const userver::components::ComponentConfig &config,
                        const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

class CancelLessonHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
  static constexpr std::string_view kName = "lesson-cancel-handler";
  CancelLessonHandler(const userver::components::ComponentConfig &config,
                      const userver::components::ComponentContext &context);

  std::string HandleRequestThrow(
      const userver::server::http::HttpRequest &request,
      userver::server::request::RequestContext &context) const override;

private:
  const LessonService &service_;
};

} // namespace tutorflow::lesson
