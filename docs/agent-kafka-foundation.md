# Этап 5D: Kafka foundation

Stage: 5D. Скоуп строго инфраструктурный: поднять Kafka и научиться
публиковать/читать тестовое событие. Бизнес-логика и outbox — 5E.

## Выбор Kafka-клиента

**Нативный userver Kafka-компонент** (`userver::kafka`) поверх librdkafka 2.14.2 —
доступен в образе `ghcr.io/userver-framework/ubuntu-22.04-userver` (CMake-таргет
`userver::kafka`, заголовки `userver/kafka/*`). Используются:
`kafka::ProducerComponent` / `kafka::Producer`, `kafka::ConsumerComponent` /
`kafka::ConsumerScope`. На этом же строится 5E (outbox publisher + consumer).

Брокеры передаются **через secdist** (`kafka_settings` → per-component-name
`{brokers, [credentials]}`), не через static_config. Безопасность для dev —
`PLAINTEXT`.

## Брокер (docker-compose)

`apache/kafka:3.8.1` в **KRaft**-режиме (одна нода broker+controller, без
zookeeper). Внутренний listener `kafka:9092`, наружу **не публикуется**, volume
`kafkadata`, сеть `tutorflow`, healthcheck через `kafka-topics.sh --list`.
Боевые сервисы пока **не** зависят от kafka (события подключатся на 5E).

Опционально dev-UI под профилем: `docker compose --profile kafka-ui up kafka-ui`
(публикует порт `${KAFKA_UI_PORT:-8090}`).

## libs/events (`tutorflow_events`)

- `event_envelope.{hpp,cpp}` — `EventEnvelope{event_id, event_type,
  event_version, occurred_at, producer, trace_id, payload(json::Value)}` +
  `MakeEvent` (генерит uuid + occurred_at) + `ToJson`/`ToJsonString`/`Parse`/
  `ParseString`. Зависит только от `userver::core` (транспорт-агностично).
- `event_publisher.{hpp,cpp}` — `EventPublisher` поверх `const kafka::Producer&`:
  `Publish(topic, key, envelope)` → JSON → `producer.Send`.
- `event_consumer.{hpp,cpp}` — `EventConsumer` поверх `kafka::ConsumerScope`
  (берётся из `ConsumerComponent` через copy-elision, хранится последним полем):
  `Start()` парсит JSON→envelope, дёргает колбэк, `AsyncCommit`. Без
  идемпотентности/ретраев (это 5E).

Линкуется `userver::core` + `userver::kafka`. Подключено в корневой CMakeLists
(`add_subdirectory(libs/events)` + `find_package(userver ... kafka)`).

## Формат события (envelope)

```json
{
  "event_id": "uuid", "event_type": "lesson.completed", "event_version": 1,
  "occurred_at": "2026-06-24T10:00:00Z", "producer": "lesson-service",
  "trace_id": "req-...", "payload": { "...самодостаточный..." }
}
```

Топики: `tutorflow.<domain>.<event>` (напр. `tutorflow.lesson.completed`),
key = id агрегата. Контракты — `docs/event-contracts/*.v1.json`.

## Static config Kafka-компонентов (важные детали)

- `kafka-producer` требует task-processor **`producer-task-processor`**;
  `kafka-consumer` — **`consumer-blocking-task-processor`** (и
  `consumer-task-processor`). Для имён длиннее `\w{1,5}-task-processor` надо явно
  задавать `thread_name` (иначе ошибка парсинга конфига).
- secdist подаётся через `secdist` + `default-secdist-provider: {config: <path>}`.
  Формат: `{"kafka_settings": {"kafka-producer": {"brokers": "kafka:9092"},
  "kafka-consumer": {"brokers": "kafka:9092"}}}`.
- consumer: `group_id`, `topics: [...]`, `auto_offset_reset: smallest`,
  `max_batch_size`, `security_protocol: PLAINTEXT`.

## Как проверить локально

Брокер: `docker compose up -d kafka` → станет `healthy`.

CLI внутри брокера:
```sh
docker compose exec kafka /opt/kafka/bin/kafka-topics.sh --bootstrap-server localhost:9092 --list
docker compose exec kafka /opt/kafka/bin/kafka-console-consumer.sh \
  --bootstrap-server localhost:9092 --topic tutorflow.dev.probe --from-beginning
```

UI: `docker compose --profile kafka-ui up -d kafka-ui` → http://localhost:8090.

### Доказанный publish→consume (тестовое событие)

Демонстрировался временным dev-сервисом `kafka-probe` (profile `kafka-probe`),
который публиковал и тут же читал событие через `libs/events`. Лог round-trip:

```
[kafka-probe] PUBLISH  event_id=e9021ab5ef29456ba4b742f2bcc68c69 topic=tutorflow.dev.probe
[kafka-probe] CONSUMED ok event_id=e9021ab5ef29456ba4b742f2bcc68c69 event_type=probe.ping
              topic=tutorflow.dev.probe key=e9021ab5ef29456ba4b742f2bcc68c69
              payload={"note":"5D kafka foundation publish->consume round-trip","answer":42}
```

Один и тот же `event_id` опубликован и прочитан, payload не искажён —
`EventPublisher`/`EventConsumer`/`EventEnvelope` работают end-to-end. Probe был
**удалён после демонстрации** (по решению: не оставлять временный код висящим);
`libs/events` остаётся готовой к использованию на 5E.

## Что НЕ делалось (5E)

- Нет `outbox_events` / transactional outbox.
- Реальные бизнес-события не подключены; `lesson → finance` charge остаётся на
  текущем internal REST.
- События не на Protobuf (JSON по решению roadmap).
- gRPC-слой, внешний REST gateway, frontend, бизнес-логика не тронуты.
