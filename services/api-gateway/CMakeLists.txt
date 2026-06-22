# api-gateway (owner: Agent A). Публичный API, auth, роутинг. Без БД.
add_executable(api-gateway src/main.cpp)

# Gateway ходит в сервисы по HTTP (userver::core http-client). БД у него нет.
target_link_libraries(api-gateway PRIVATE tutorflow_common userver::core)
