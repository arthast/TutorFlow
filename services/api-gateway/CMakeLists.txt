# api-gateway. Публичный API, auth, роутинг. Без БД.
add_executable(api-gateway
  src/main.cpp
  src/clients/identity_grpc_client.cpp
  src/cors.cpp
  src/gateway_settings.cpp
  src/handlers/health_handler.cpp
  src/handlers/proxy_handlers.cpp
)

target_include_directories(api-gateway PRIVATE src)

# Gateway keeps external REST, but may use HTTP or gRPC internally. БД у него нет.
target_link_libraries(api-gateway PRIVATE tutorflow_common tutorflow_grpc_clients userver::core userver::grpc)
