# finance-service (owner: Agent B). Append-only журнал операций + чеки.
add_executable(finance-service src/main.cpp)

# Реализуя репозитории, добавь userver::postgresql.
target_link_libraries(finance-service PRIVATE tutorflow_common userver::core)
