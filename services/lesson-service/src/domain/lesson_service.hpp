#pragma once

#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>

#include "domain/models.hpp"

namespace tutorflow::common {
struct AuthContext;
}

namespace tutorflow::clients {
class IdentityClient;
}

namespace tutorflow::lesson {

class LessonRepository;

class LessonService final : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "lesson-domain-service";

  LessonService(const userver::components::ComponentConfig &config,
                const userver::components::ComponentContext &context);

  Slot CreateSlot(const tutorflow::common::AuthContext &auth,
                  const CreateSlotRequest &request) const;
  std::vector<Slot> ListSlots(const tutorflow::common::AuthContext &auth) const;

  Lesson CreateLesson(const tutorflow::common::AuthContext &auth,
                      const CreateLessonRequest &request) const;
  std::vector<Lesson>
  ListLessons(const tutorflow::common::AuthContext &auth) const;
  Lesson GetLesson(const std::string &lesson_id) const;
  CompleteLessonOutcome CompleteLesson(const tutorflow::common::AuthContext &auth,
                                       const std::string &lesson_id) const;
  Lesson RescheduleLesson(const tutorflow::common::AuthContext &auth,
                          const RescheduleLessonRequest &request) const;
  Lesson ReactivateLesson(const tutorflow::common::AuthContext &auth,
                          const std::string &lesson_id) const;
  Lesson CancelLesson(const tutorflow::common::AuthContext &auth,
                      const std::string &lesson_id) const;

private:
  LessonRepository &repository_;
  tutorflow::clients::IdentityClient &identity_;
};

} // namespace tutorflow::lesson
