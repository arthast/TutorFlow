# syntax=docker/dockerfile:1
ARG USERVER_IMAGE=ghcr.io/userver-framework/ubuntu-22.04-userver:latest

FROM ${USERVER_IMAGE}
ARG SERVICE=realtime-service
ARG BUILD_JOBS=
WORKDIR /src

COPY CMakeLists.txt ./
COPY libs ./libs
COPY services/${SERVICE} ./services/${SERVICE}

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target ${SERVICE} -j "${BUILD_JOBS:-$(nproc)}"

WORKDIR /app
RUN cp "/src/build/services/${SERVICE}/${SERVICE}" /app/service \
 && cp -r "/src/services/${SERVICE}/configs" /app/configs

EXPOSE 8089
ENTRYPOINT ["/app/service", "--config", "/app/configs/static_config.yaml"]
