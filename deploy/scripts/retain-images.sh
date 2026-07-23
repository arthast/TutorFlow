#!/usr/bin/env bash

set -euo pipefail

docker_bin="${DOCKER_BIN:-docker}"

usage() {
  cat >&2 <<'EOF'
Usage:
  retain-images.sh remember --namespace <namespace> --state-file <path>
  retain-images.sh prune --namespace <namespace> --current-tag <tag> --state-file <path>
EOF
  exit 2
}

is_valid_namespace() {
  local value="$1"
  local segment
  local -a namespace_segments=()

  [[ "$value" =~ ^[a-zA-Z0-9.-]+(:[0-9]+)?(/[a-zA-Z0-9._-]+)*$ ]] || return 1
  IFS='/' read -r -a namespace_segments <<<"$value"
  for segment in "${namespace_segments[@]}"; do
    [[ "$segment" != "." && "$segment" != ".." ]] || return 1
  done
}

is_valid_tag() {
  [[ "$1" =~ ^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$ ]]
}

contains_exact() {
  local needle="$1"
  local item
  shift
  for item in "$@"; do
    [[ "$item" == "$needle" ]] && return 0
  done
  return 1
}

namespace=""
current_tag_arg=""
state_file=""
command="${1:-}"
[[ -n "$command" ]] || usage
shift

while (($# > 0)); do
  case "$1" in
    --namespace)
      (($# >= 2)) || usage
      namespace="$2"
      shift 2
      ;;
    --current-tag)
      (($# >= 2)) || usage
      current_tag_arg="$2"
      shift 2
      ;;
    --state-file)
      (($# >= 2)) || usage
      state_file="$2"
      shift 2
      ;;
    *)
      usage
      ;;
  esac
done

[[ "$command" == "remember" || "$command" == "prune" ]] || usage
[[ -n "$namespace" && -n "$state_file" ]] || usage
is_valid_namespace "$namespace" || {
  echo "Invalid image namespace: $namespace" >&2
  exit 2
}
if [[ "$command" == "prune" ]]; then
  [[ -n "$current_tag_arg" ]] || usage
  is_valid_tag "$current_tag_arg" || {
    echo "Invalid current image tag: $current_tag_arg" >&2
    exit 2
  }
elif [[ -n "$current_tag_arg" ]]; then
  usage
fi

state_current=""
state_previous=""

load_state() {
  local line key value
  local seen_current=0
  local seen_previous=0

  [[ -e "$state_file" ]] || return 0
  [[ -f "$state_file" ]] || {
    echo "Image retention state is not a regular file: $state_file" >&2
    return 1
  }

  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ "$line" == *=* ]] || {
      echo "Invalid image retention state line" >&2
      return 1
    }
    key="${line%%=*}"
    value="${line#*=}"
    case "$key" in
      current)
        ((seen_current == 0)) || {
          echo "Duplicate current tag in image retention state" >&2
          return 1
        }
        state_current="$value"
        seen_current=1
        ;;
      previous)
        ((seen_previous == 0)) || {
          echo "Duplicate previous tag in image retention state" >&2
          return 1
        }
        state_previous="$value"
        seen_previous=1
        ;;
      *)
        echo "Unknown key in image retention state: $key" >&2
        return 1
        ;;
    esac
  done <"$state_file"

  ((seen_current == 1 && seen_previous == 1)) || {
    echo "Incomplete image retention state" >&2
    return 1
  }
  is_valid_tag "$state_current" || {
    echo "Invalid current tag in image retention state" >&2
    return 1
  }
  if [[ -n "$state_previous" ]]; then
    is_valid_tag "$state_previous" || {
      echo "Invalid previous tag in image retention state" >&2
      return 1
    }
  fi
}

write_state() {
  local next_current="$1"
  local next_previous="$2"
  local state_dir temp_file

  is_valid_tag "$next_current" || {
    echo "Refusing to write an invalid current tag" >&2
    return 1
  }
  if [[ -n "$next_previous" ]]; then
    is_valid_tag "$next_previous" || {
      echo "Refusing to write an invalid previous tag" >&2
      return 1
    }
  fi

  state_dir="$(dirname "$state_file")"
  [[ -d "$state_dir" ]] || {
    echo "Image retention state directory does not exist: $state_dir" >&2
    return 1
  }

  temp_file="$(mktemp "${state_file}.tmp.XXXXXX")"
  trap 'rm -f -- "$temp_file"' RETURN
  {
    printf 'current=%s\n' "$next_current"
    printf 'previous=%s\n' "$next_previous"
  } >"$temp_file"
  chmod 0600 "$temp_file"
  mv -f -- "$temp_file" "$state_file"
  trap - RETURN
}

remember_running_release() {
  local image prefix tag running_output
  local -a running_tags=()
  prefix="${namespace}/tutorflow-api-gateway:"

  load_state
  if [[ -n "$state_current" ]]; then
    echo "Image retention state already exists; it changes only after a successful health check"
    return 0
  fi
  running_output="$("$docker_bin" ps --format '{{.Image}}')"

  while IFS= read -r image; do
    [[ "$image" == "$prefix"* ]] || continue
    tag="${image#"$prefix"}"
    is_valid_tag "$tag" || continue
    if ((${#running_tags[@]} == 0)) ||
      ! contains_exact "$tag" "${running_tags[@]}"; then
      running_tags+=("$tag")
    fi
  done <<<"$running_output"

  if ((${#running_tags[@]} == 0)); then
    echo "No running TutorFlow API gateway image found; retention state is unchanged" >&2
    return 0
  fi
  if ((${#running_tags[@]} != 1)); then
    echo "Multiple running TutorFlow API gateway releases found; retention state is unchanged" >&2
    return 1
  fi

  tag="${running_tags[0]}"
  write_state "$tag" ""
}

prune_old_releases() {
  local next_current next_previous ref repository tag containers_output images_output
  local -a used_images=()

  load_state
  if [[ -z "$state_current" ]]; then
    next_current="$current_tag_arg"
    next_previous=""
  elif [[ "$state_current" == "$current_tag_arg" ]]; then
    next_current="$state_current"
    next_previous="$state_previous"
  else
    next_current="$current_tag_arg"
    next_previous="$state_current"
  fi

  containers_output="$("$docker_bin" ps -a --format '{{.Image}}')"
  while IFS= read -r ref; do
    [[ -n "$ref" ]] && used_images+=("$ref")
  done <<<"$containers_output"

  images_output="$("$docker_bin" image ls --format '{{.Repository}}:{{.Tag}}')"
  while IFS= read -r ref; do
    [[ "$ref" == "${namespace}/tutorflow-"*:* ]] || continue
    repository="${ref%:*}"
    tag="${ref##*:}"
    [[ "$repository" == "${namespace}/tutorflow-"* ]] || continue
    is_valid_tag "$tag" || continue
    [[ "$tag" != "$next_current" && "$tag" != "$next_previous" ]] || continue
    if ((${#used_images[@]} > 0)) &&
      contains_exact "$ref" "${used_images[@]}"; then
      continue
    fi
    "$docker_bin" image rm "$ref"
  done <<<"$images_output"

  "$docker_bin" image prune -f
  write_state "$next_current" "$next_previous"
}

case "$command" in
  remember)
    remember_running_release
    ;;
  prune)
    prune_old_releases
    ;;
esac
