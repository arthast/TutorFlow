# file-service (owner: Agent A). Метаданные файлов + локальный том.
add_executable(file-service src/main.cpp)

# Реализуя репозитории, добавь userver::postgresql.
target_link_libraries(file-service PRIVATE tutorflow_common userver::core)
