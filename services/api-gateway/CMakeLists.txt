# api-gateway. Публичный API, auth, роутинг. Без БД.
add_executable(api-gateway
  src/main.cpp
  src/gateway_settings.cpp
  src/handlers/proxy_handlers.cpp
)

target_include_directories(api-gateway PRIVATE src)

# Gateway ходит в сервисы по HTTP (userver::core http-client). БД у него нет.
target_link_libraries(api-gateway PRIVATE tutorflow_common userver::core)
