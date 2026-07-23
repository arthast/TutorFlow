#!/usr/bin/env bash
#
# Применение SQL-миграций в dev
#
#   ./scripts/migrate.sh all                 # все сервисы
#   ./scripts/migrate.sh identity            # один сервис
#   ./scripts/migrate.sh identity lesson     # несколько
#
# Применяет migrations/<service>/*.sql по возрастанию имени в соответствующую БД
# через работающий контейнер postgres (docker compose exec). Порт postgres
# наружу не публикуется, поэтому ходим внутрь контейнера.
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# .env нужен для POSTGRES_USER/POSTGRES_PASSWORD.
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi
: "${POSTGRES_USER:?POSTGRES_USER is not set (cp .env.example .env)}"

# service -> one or more databases
dbs_for() {
  case "$1" in
    identity)   echo "${IDENTITY_DB:-identity_db}" ;;
    lesson)     echo "${LESSON_DB:-lesson_db}" ;;
    assignment) echo "${ASSIGNMENT_DB:-assignment_db}" ;;
    finance)    echo "${FINANCE_DB:-finance_db}" ;;
    file)       echo "${FILE_DB:-file_db}" ;;
    notification) echo "${NOTIFICATION_DB:-notification_db}" ;;
    report) echo "${REPORT_DB:-report_db}" ;;
    chat) echo "${CHAT_DB_SHARD0:-chat_db_shard0} ${CHAT_DB_SHARD1:-chat_db_shard1}" ;;
    *) echo "" ;;
  esac
}

ALL_SERVICES=(identity lesson assignment finance file notification report chat)

DC="docker compose"
$DC version >/dev/null 2>&1 || DC="docker-compose"

apply_service() {
  local svc="$1"
  local dbs
  dbs="$(dbs_for "$svc")"
  if [[ -z "$dbs" ]]; then
    echo "!! unknown service: $svc (expected: ${ALL_SERVICES[*]})" >&2
    return 1
  fi

  local dir="migrations/$svc"
  if [[ ! -d "$dir" ]]; then
    echo "-- $svc: no migrations dir ($dir), skip"
    return 0
  fi

  shopt -s nullglob
  local files=("$dir"/*.sql)
  shopt -u nullglob
  if [[ ${#files[@]} -eq 0 ]]; then
    echo "-- $svc: no .sql files in $dir, skip"
    return 0
  fi

  IFS=$'\n' files=($(sort <<<"${files[*]}")); unset IFS

  local db
  for db in $dbs; do
    # initdb scripts run only for a fresh postgres volume. Create newly added
    # service databases here as well so existing dev volumes keep working.
    $DC exec -T postgres \
      createdb -U "$POSTGRES_USER" "$db" >/dev/null 2>&1 || true

    echo "== $svc -> $db"
    for f in "${files[@]}"; do
      echo "   applying $(basename "$f")"
      $DC exec -T postgres \
        psql -v ON_ERROR_STOP=1 -U "$POSTGRES_USER" -d "$db" < "$f"
    done
  done
}

main() {
  if [[ $# -eq 0 ]]; then
    echo "usage: $0 all | <service> [<service> ...]" >&2
    echo "services: ${ALL_SERVICES[*]}" >&2
    exit 2
  fi

  local targets=()
  if [[ "$1" == "all" ]]; then
    targets=("${ALL_SERVICES[@]}")
  else
    targets=("$@")
  fi

  for svc in "${targets[@]}"; do
    apply_service "$svc"
  done
  echo "done."
}

main "$@"
