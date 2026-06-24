#pragma once

#include <optional>
#include <string>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::lesson {

class LessonRepository final
    : public userver::components::LoggableComponentBase {
public:
  static constexpr std::string_view kName = "lesson-repository";

  LessonRepository(const userver::components::ComponentConfig &config,
                   const userver::components::ComponentContext &context);

  Slot CreateSlot(const std::string &teacher_id,
                  const CreateSlotRequest &request) const;
  std::vector<Slot> ListSlots(const std::string &teacher_id) const;

  Lesson CreateLesson(const std::string &teacher_id,
                      const CreateLessonRequest &request) const;
  std::vector<Lesson>
  ListLessonsForTeacher(const std::string &teacher_id) const;
  std::vector<Lesson>
  ListLessonsForStudent(const std::string &student_id) const;
  std::optional<Lesson> FindLesson(const std::string &lesson_id) const;
  Lesson CompleteLesson(const std::string &lesson_id,
                        const std::string &teacher_id) const;
  Lesson RescheduleLesson(const std::string &teacher_id,
                          const RescheduleLessonRequest &request) const;
  Lesson CancelLesson(const std::string &lesson_id,
                      const std::string &teacher_id) const;

private:
  userver::storages::postgres::ClusterPtr pg_;
};

} // namespace tutorflow::lesson
