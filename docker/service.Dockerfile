# syntax=docker/dockerfile:1
ARG USERVER_IMAGE=ghcr.io/userver-framework/ubuntu-22.04-userver:v3.1@sha256:c08af6bf58f07a472376ed0bb74165e3d96fb5c8f4e07a3f0b5e11d5d0183f5b

FROM ${USERVER_IMAGE}
ARG SERVICE
ARG BUILD_JOBS=
WORKDIR /src

COPY CMakeLists.txt ./
COPY libs ./libs
COPY services/${SERVICE} ./services/${SERVICE}

RUN test -n "${SERVICE}" \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target "${SERVICE}" -j "${BUILD_JOBS:-$(nproc)}"

WORKDIR /app
RUN cp "/src/build/services/${SERVICE}/${SERVICE}" /app/service \
 && cp -r "/src/services/${SERVICE}/configs" /app/configs

ENTRYPOINT ["/app/service", "--config", "/app/configs/static_config.yaml"]
