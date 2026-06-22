# assignment-service (owner: Agent B). ДЗ, решения, комментарии.
add_executable(assignment-service src/main.cpp)

# Реализуя репозитории, добавь userver::postgresql.
target_link_libraries(assignment-service PRIVATE tutorflow_common userver::core)
