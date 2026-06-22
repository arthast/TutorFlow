# lesson-service (owner: Agent B). Расписание + занятия.
add_executable(lesson-service src/main.cpp)

# Реализуя репозитории, добавь userver::postgresql.
target_link_libraries(lesson-service PRIVATE tutorflow_common userver::core)
