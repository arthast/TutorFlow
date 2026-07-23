# syntax=docker/dockerfile:1
ARG USERVER_IMAGE=ghcr.io/userver-framework/ubuntu-22.04-userver:v3.1@sha256:c08af6bf58f07a472376ed0bb74165e3d96fb5c8f4e07a3f0b5e11d5d0183f5b
ARG RUNTIME_IMAGE=ubuntu:22.04@sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982

FROM ${USERVER_IMAGE} AS builder
ARG SERVICE
ARG BUILD_JOBS=
WORKDIR /src

COPY CMakeLists.txt ./
COPY libs ./libs
COPY services/${SERVICE} ./services/${SERVICE}

RUN test -n "${SERVICE}" \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target "${SERVICE}" -j "${BUILD_JOBS:-$(nproc)}"

RUN binary="/src/build/services/${SERVICE}/${SERVICE}" \
 && test -x "${binary}" \
 && install -D -m 0755 "${binary}" /runtime/service \
 && cp -a "/src/services/${SERVICE}/configs" /runtime/configs \
 && mkdir -p \
      /runtime-root/lib \
      /runtime-root/usr/lib \
      /runtime-root/usr/local/lib \
 && ldd "${binary}" \
      | awk '/=> \// {print $3} /^\// {print $1}' \
      | sort -u \
      | xargs -r -I '{}' cp -L --parents '{}' /runtime-root \
 && ! ldd "${binary}" | grep -q 'not found'

FROM ${RUNTIME_IMAGE} AS runtime

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /runtime-root/lib/ /usr/lib/
COPY --from=builder /runtime-root/usr/lib/ /usr/lib/
COPY --from=builder /runtime-root/usr/local/lib/ /usr/local/lib/
COPY --from=builder /runtime/service /app/service
COPY --from=builder /runtime/configs /app/configs

ENTRYPOINT ["/app/service", "--config", "/app/configs/static_config.yaml"]
