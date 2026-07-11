#!/usr/bin/env bash
set -Eeuo pipefail

CLUSTER_NAME="tutorflow"
NAMESPACE="tutorflow"
INGRESS_NGINX_VERSION="v1.15.1"
METRICS_SERVER_VERSION="v0.8.1"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BASE_DIR="$SCRIPT_DIR/base"
OVERLAY_DIR="$SCRIPT_DIR/overlays/local"
ENV_FILE="${TUTORFLOW_ENV_FILE:-$ROOT_DIR/.env}"

INGRESS_MANIFEST="https://raw.githubusercontent.com/kubernetes/ingress-nginx/controller-${INGRESS_NGINX_VERSION}/deploy/static/provider/kind/deploy.yaml"
METRICS_SERVER_MANIFEST="https://github.com/kubernetes-sigs/metrics-server/releases/download/${METRICS_SERVER_VERSION}/components.yaml"

log() {
  printf '\n==> %s\n' "$*"
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "required command is missing: $1"
}

cluster_exists() {
  kind get clusters 2>/dev/null | grep -Fxq "$CLUSTER_NAME" &&
    [[ "$(docker inspect --format '{{.State.Running}}' "${CLUSTER_NAME}-control-plane" 2>/dev/null)" == "true" ]] &&
    kubectl cluster-info --context "kind-$CLUSTER_NAME" >/dev/null 2>&1
}

cluster_is_registered() {
  kind get clusters 2>/dev/null | grep -Fxq "$CLUSTER_NAME"
}

check_host_port() {
  local port="$1"
  if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
    die "host TCP port $port is already in use; free it before creating the kind cluster"
  fi
}

load_environment() {
  if [[ ! -f "$ENV_FILE" ]]; then
    ENV_FILE="$ROOT_DIR/.env.example"
    echo "Using .env.example for local-only demo secrets because .env is absent."
  fi

  set -a
  # shellcheck disable=SC1090
  source "$ENV_FILE"
  set +a

  : "${POSTGRES_USER:=tutorflow}"
  : "${POSTGRES_PASSWORD:=change_me}"
  : "${JWT_SECRET:=change_me_jwt_secret}"
  : "${MINIO_ROOT_USER:=tutorflow}"
  : "${MINIO_ROOT_PASSWORD:=tutorflow_minio_password}"
  : "${FILE_S3_BUCKET:=tutorflow-files}"
  : "${FILE_S3_REGION:=us-east-1}"
}

database_url() {
  local database="$1"
  python3 - "$POSTGRES_USER" "$POSTGRES_PASSWORD" "$database" <<'PY'
import sys
from urllib.parse import quote

user, password, database = sys.argv[1:]
print(
    "postgresql://"
    + quote(user, safe="")
    + ":"
    + quote(password, safe="")
    + "@postgres.tutorflow.svc.cluster.local:5432/"
    + quote(database, safe="")
)
PY
}

generate_runtime_config() {
  local kafka_secdist file_secdist
  kafka_secdist='{"kafka_settings":{"kafka-producer":{"brokers":"kafka:9092"},"kafka-consumer":{"brokers":"kafka:9092"}}}'
  file_secdist="$(python3 - "$MINIO_ROOT_USER" "$MINIO_ROOT_PASSWORD" "$FILE_S3_BUCKET" "$FILE_S3_REGION" <<'PY'
import json
import sys

access_key, secret_key, bucket, region = sys.argv[1:]
print(json.dumps({
    "s3_file_storage": {
        "endpoint": "http://minio.tutorflow.svc.cluster.local:9000",
        "access_key": access_key,
        "secret_key": secret_key,
        "bucket": bucket,
        "region": region,
    }
}, separators=(",", ":")))
PY
)"

  kubectl create secret generic tutorflow-secrets \
    --namespace "$NAMESPACE" \
    --from-literal=POSTGRES_PASSWORD="$POSTGRES_PASSWORD" \
    --from-literal=JWT_SECRET="$JWT_SECRET" \
    --from-literal=MINIO_ROOT_USER="$MINIO_ROOT_USER" \
    --from-literal=MINIO_ROOT_PASSWORD="$MINIO_ROOT_PASSWORD" \
    --from-literal=IDENTITY_DATABASE_URL="$(database_url identity_db)" \
    --from-literal=LESSON_DATABASE_URL="$(database_url lesson_db)" \
    --from-literal=ASSIGNMENT_DATABASE_URL="$(database_url assignment_db)" \
    --from-literal=FINANCE_DATABASE_URL="$(database_url finance_db)" \
    --from-literal=FILE_DATABASE_URL="$(database_url file_db)" \
    --from-literal=NOTIFICATION_DATABASE_URL="$(database_url notification_db)" \
    --from-literal=REPORT_DATABASE_URL="$(database_url report_db)" \
    --from-literal=CHAT_DATABASE_URL_SHARD0="$(database_url chat_db_shard0)" \
    --from-literal=CHAT_DATABASE_URL_SHARD1="$(database_url chat_db_shard1)" \
    --from-literal=KAFKA_SECDIST_CONFIG="$kafka_secdist" \
    --from-literal=FILE_SECDIST_CONFIG="$file_secdist" \
    --dry-run=client -o yaml | kubectl apply -f -

  kubectl create configmap postgres-initdb \
    --namespace "$NAMESPACE" \
    --from-file="01-create-databases.sql=$ROOT_DIR/docker/postgres/initdb/01-create-databases.sql" \
    --dry-run=client -o yaml | kubectl apply -f -

  local service
  for service in identity lesson assignment finance file notification report chat; do
    kubectl create configmap "migrations-$service" \
      --namespace "$NAMESPACE" \
      --from-file="$ROOT_DIR/migrations/$service" \
      --dry-run=client -o yaml | kubectl apply -f -
  done
}

install_addons() {
  log "Installing ingress-nginx ${INGRESS_NGINX_VERSION}"
  kubectl apply -f "$INGRESS_MANIFEST"
  kubectl rollout status deployment/ingress-nginx-controller \
    --namespace ingress-nginx --timeout=300s

  log "Installing metrics-server ${METRICS_SERVER_VERSION}"
  kubectl apply -f "$METRICS_SERVER_MANIFEST"
  if ! kubectl get deployment metrics-server -n kube-system \
      -o jsonpath='{.spec.template.spec.containers[0].args}' | \
      grep -Fq -- '--kubelet-insecure-tls'; then
    kubectl patch deployment metrics-server -n kube-system --type=json \
      -p='[{"op":"add","path":"/spec/template/spec/containers/0/args/-","value":"--kubelet-insecure-tls"}]'
  fi
  kubectl rollout status deployment/metrics-server -n kube-system --timeout=300s
  kubectl wait --for=condition=Available apiservice/v1beta1.metrics.k8s.io --timeout=300s
}

build_and_load_images() {
  if [[ "${KIND_SKIP_BUILD:-0}" == "1" ]]; then
    log "Skipping image builds because KIND_SKIP_BUILD=1"
  else
    log "Building TutorFlow service images with Docker Compose"
  local compose_services=(
    api-gateway
    identity-service
    lesson-service
    assignment-service
    finance-service
    file-service
    notification-service
    report-service
    chat-service
    realtime-service
  )
  local service
  for service in "${compose_services[@]}"; do
    log "Building image for $service"
    local attempt
    for attempt in 1 2 3; do
      if docker compose build "$service"; then
        break
      fi
      if [[ "$attempt" == "3" ]]; then
        die "failed to build $service after $attempt attempts"
      fi
      echo "Build for $service failed (attempt $attempt/3); retrying..." >&2
      sleep $((attempt * 5))
    done
  done

    log "Building frontend for same-origin ingress"
    docker build \
      -f frontend/Dockerfile \
      --build-arg VITE_API_URL=http://localhost \
      --build-arg VITE_REALTIME_URL=ws://localhost/ws \
      -t tutorflow-frontend:latest \
      .
  fi

  local images=(
    tutorflow-api-gateway:latest
    tutorflow-identity-service:latest
    tutorflow-lesson-service:latest
    tutorflow-assignment-service:latest
    tutorflow-finance-service:latest
    tutorflow-file-service:latest
    tutorflow-notification-service:latest
    tutorflow-report-service:latest
    tutorflow-chat-service:latest
    tutorflow-realtime-service:latest
    tutorflow-frontend:latest
  )

  local image
  for image in "${images[@]}"; do
    docker image inspect "$image" >/dev/null 2>&1 || \
      die "required local image is missing: $image"
  done

  log "Loading local images into kind"
  kind load docker-image --name "$CLUSTER_NAME" "${images[@]}"
}

apply_stack() {
  log "Creating namespace and runtime configuration"
  kubectl apply -f "$BASE_DIR/namespace.yaml"
  kubectl apply --namespace "$NAMESPACE" -f "$BASE_DIR/app-config.yaml"
  generate_runtime_config

  log "Starting persistent infrastructure"
  kubectl apply --namespace "$NAMESPACE" \
    -f "$BASE_DIR/postgres.yaml" \
    -f "$BASE_DIR/kafka.yaml" \
    -f "$BASE_DIR/redis.yaml" \
    -f "$BASE_DIR/minio.yaml"
  local statefulset
  for statefulset in postgres kafka redis minio; do
    kubectl rollout status "statefulset/$statefulset" \
      --namespace "$NAMESPACE" --timeout=600s
  done

  log "Running idempotent database and Kafka initialization Jobs"
  kubectl delete job migrator kafka-init --namespace "$NAMESPACE" \
    --ignore-not-found --wait=true
  kubectl apply --namespace "$NAMESPACE" \
    -f "$BASE_DIR/migrator.yaml" \
    -f "$BASE_DIR/kafka-init.yaml"
  kubectl wait --namespace "$NAMESPACE" \
    --for=condition=complete job/migrator job/kafka-init \
    --timeout=600s

  log "Applying local kustomize overlay"
  kubectl apply -k "$OVERLAY_DIR"

  local deployment
  for deployment in api-gateway identity-service lesson-service assignment-service \
      finance-service file-service notification-service report-service chat-service \
      realtime-service frontend; do
    kubectl rollout restart "deployment/$deployment" --namespace "$NAMESPACE"
    kubectl rollout status "deployment/$deployment" \
      --namespace "$NAMESPACE" --timeout=600s
  done
}

main() {
  require_command docker
  require_command kind
  require_command kubectl
  require_command python3
  require_command curl
  require_command lsof

  cd "$ROOT_DIR"
  load_environment

  log "Active Docker context: $(docker context show)"
  docker info >/dev/null 2>&1 || die "Docker daemon is unavailable in the active context"

  if cluster_exists; then
    log "Reusing existing kind cluster '$CLUSTER_NAME'"
  else
    if cluster_is_registered; then
      log "Removing unhealthy kind cluster '$CLUSTER_NAME'"
      kind delete cluster --name "$CLUSTER_NAME"
    fi
    check_host_port 80
    check_host_port 443
    log "Creating kind cluster '$CLUSTER_NAME'"
    kind create cluster --name "$CLUSTER_NAME" --config "$SCRIPT_DIR/kind-config.yaml"
  fi
  kubectl config use-context "kind-$CLUSTER_NAME" >/dev/null

  install_addons
  build_and_load_images
  apply_stack

  log "Running MVP smoke through ingress"
  GATEWAY_URL=http://localhost python3 scripts/smoke_mvp.py

  log "TutorFlow Kubernetes resources"
  kubectl get pods,svc,ingress,hpa --namespace "$NAMESPACE"
}

main "$@"
