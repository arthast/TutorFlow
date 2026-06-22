# identity-service (owner: Agent A). Auth + профили + связь teacher-student.
add_executable(identity-service src/main.cpp)

# tutorflow_common тянет userver::core транзитивно; userver::core указан явно
# для читаемости. Реализуя репозитории, добавь userver::postgresql.
target_link_libraries(identity-service PRIVATE tutorflow_common userver::core)
