#!/usr/bin/env bash
set -Eeuo pipefail

CLUSTER_NAME="tutorflow"

if ! command -v kind >/dev/null 2>&1; then
  echo "kind is not installed; cluster '$CLUSTER_NAME' cannot exist" >&2
  exit 0
fi

if kind get clusters 2>/dev/null | grep -Fxq "$CLUSTER_NAME"; then
  kind delete cluster --name "$CLUSTER_NAME"
else
  echo "kind cluster '$CLUSTER_NAME' is already absent"
fi

