# Задача: Этап 8.2 — Readiness checks (+ верификация 8.1)

> Контекст и правила: `AGENTS.md`, `docs/roadmap.md` (секция «Этап 8», пункты
> 8.1 и 8.2), `docs/adr/0003-service-replicas-and-kafka-scaling.md`,
> `docs/adr/0004-kubernetes-deploy.md` (probes). Прочитать ПЕРЕД началом.

## Git-правила этой сессии (важно, отличается от AGENTS.md)

- Работать в ТЕКУЩЕЙ ветке. НЕ создавать веток, НЕ делать `git commit`,
  `git push`, `git checkout`, `git stash` — все git-решения принимает человек.
- В дереве уже лежат незакоммиченные изменения Этапа 8.1
  (`libs/events/src/outbox_publisher.cpp` — advisory-lock лидер в outbox,
  `docs/roadmap.md`, `docs/EVENTS.md`, `docs/adr/000{2,3,4}-*.md`). Их НЕ
  откатывать и не переписывать; код 8.1 менять только если он не собирается
  (см. Шаг 0).

## Шаг 0 — верификация 8.1 (блокирует всё остальное)

1. `docker compose build notification-service` (соберёт `libs/events`).
   Если сборка падает в `outbox_publisher.cpp` — починить МИНИМАЛЬНО,
   сохранив семантику: транзакция на tick, `pg_try_advisory_xact_lock`
   первым statement'ом, batch-atomic mark, ранний выход без публикации, если
   лок не взят. Вероятная точка риска: вызов `pg_->Begin(...)` в const-методе
   (паттерн уже используется в `services/chat-service/src/repositories/
   chat_repository.cpp` — сверяться с ним).
2. `docker compose up --build -d` + `./scripts/migrate.sh all` (если нужно) +
   `python3 scripts/smoke_mvp.py` → `SMOKE OK`.
3. Быстрая проверка лидер-лока:
   `docker compose up -d --scale notification-service=2`, затем создать
   доменное событие (например, complete занятия через API) и убедиться по
   логам обоих контейнеров, что строки `[outbox] published` пишет только ОДИН
   инстанс, а дублей уведомления нет. Результат зафиксировать в отчёте.

## Задача 8.2 — Readiness checks

### Что сделать

Во всех app-сервисах развести liveness и readiness:

- `/health` — остаётся как есть: лёгкий liveness БЕЗ обращений к внешним
  зависимостям (userver `health-handler`). Не трогать.
- `/ready` — НОВЫЙ endpoint на том же HTTP-листенере, проверяет ТОЛЬКО
  собственные критичные зависимости сервиса:

| Сервис | Проверки в /ready |
|---|---|
| identity, lesson, assignment, finance, notification, report, chat | `SELECT 1` в свою БД |
| file-service | `SELECT 1` в file_db; при `FILE_STORAGE_BACKEND=s3` — дополнительно лёгкая проверка S3 (например, HEAD/bucket-exists) |
| realtime-service | PING в Redis |
| api-gateway | внешних зависимостей нет → статический 200 (готов, как только поднялся сервер) |

Правила проверок:
- НЕ проверять чужие сервисы (identity из lesson и т.п.) и НЕ проверять Kafka:
  недоступность брокера не должна выводить сервис из gRPC-балансировки
  (consumer ретраится сам). Это зафиксировано в roadmap 8.2.
- Таймаут на каждую проверку ~1s (для pg — `SELECT 1` с коротким timeout;
  не использовать тяжёлые запросы).
- Ответ: `200 {"status":"ready"}` когда всё живо;
  `503 {"status":"not_ready","failed":["postgres"]}` при отказе любой
  проверки. Формат простой, error-envelope здесь не нужен (это не доменный API).

### Реализация (ориентир, не догма)

- Простой вариант предпочтителен: маленький handler-компонент в КАЖДОМ сервисе
  (`src/handlers/ready_handler.{hpp,cpp}` — по образцу существующих компонентов
  сервиса), который берёт `Postgres`-компонент своего сервиса и делает `SELECT 1`.
  Дублирование ~40 строк на сервис — приемлемо (правило «не плодить
  преждевременных универсальных абстракций»).
- Если станет очевидно, что общий компонент в `libs/common` сильно чище —
  СТОП: это изменение public-сигнатур libs → сначала описать предложение в
  отчёте и согласовать с человеком (правило AGENTS.md про контракты).
  В `libs/common` нельзя тащить pg-зависимость.
- Регистрация в `static_config.yaml` каждого сервиса: `ready-handler` с
  `path: /ready`, `method: GET`, `task_processor: main-task-processor`.

### Compose healthchecks

- В `docker-compose.yml` (dev) добавить healthcheck app-сервисам на `/ready`.
  Сначала проверить, чем щупать endpoint изнутри контейнера (есть ли
  `curl`/`wget` в образе — `docker compose run --rm --entrypoint sh
  api-gateway -c 'which curl wget'`); если ничего нет — использовать
  bash `/dev/tcp` или добавить curl не нужно — согласовать в отчёте.
- gateway: `depends_on: condition: service_healthy` на доменные сервисы —
  добавить ТОЛЬКО если это не ломает время старта dev-стека заметно
  (проверить `docker compose up` с нуля).
- `docker-compose.prod.yml` — те же healthchecks (аккуратно: файл общий,
  ничего кроме healthcheck/depends_on не менять).

### Документация

- `docs/roadmap.md`: пункт 8.2 → ✅ с датой; пункт 5 «Актуального плана»
  пометить закрытым (ссылка на 8.2); статус 8.1 → ✅ после Шага 0.
- `docs/architecture.md`: одно-два предложения про `/health` vs `/ready`
  (в раздел про компоненты или auth/принципы — по месту).
- `docs/api-contracts/gateway.openapi.yaml` НЕ трогать: `/ready` — внутренний
  операционный endpoint, наружу через Caddy/gateway он не публикуется.

### Чего НЕ делать

- Не менять Kafka-топологию, партиции, реплики (это 8.3), не трогать
  k8s (8.6), не начинать шардинг (8.5).
- Не рефакторить существующие handlers/сервисы «заодно».
- Не публиковать новые порты наружу.
- Не добавлять зависимостей (никаких новых библиотек для health-чеков).

## Definition of Done

1. Все сервисы собираются; `docker compose up --build` зелёный.
2. `curl http://<svc>/ready` внутри сети → 200 при живых зависимостях
   (проверить минимум: identity, file (s3-режим), realtime, gateway).
3. `docker compose stop postgres` → `/ready` доменного сервиса отдаёт 503,
   при этом `/health` того же сервиса продолжает отдавать 200;
   `docker compose start postgres` → `/ready` снова 200 без рестарта сервиса.
4. `docker compose ps` показывает healthy у app-сервисов.
5. `python3 scripts/smoke_mvp.py` → `SMOKE OK`.
6. Шаг 0 выполнен: сборка 8.1 подтверждена, лидер-лок проверен на 2 репликах.

## Отчёт по завершении (обязательно)

- список изменённых файлов;
- что реализовано / что осознанно НЕ реализовано;
- результат Шага 0 (логи лидер-лока — какой инстанс публиковал);
- команды для проверки;
- вопросы/предложения по контрактам, если возникли (НЕ реализовывать молча).
