# TutorFlow

TutorFlow — учебная платформа для связки «преподаватель — ученик»: расписание,
занятия, домашние задания, файлы, чеки об оплате и ручное подтверждение платежей.

Проект сделан как microservices-lite монорепозиторий на **C++20 + userver**.
Снаружи открыт только `api-gateway`; внутренние сервисы общаются по gRPC, а
побочные доменные эффекты идут через Kafka.

## Что Уже Есть

- Регистрация, логин, JWT-auth и смена пароля.
- Роли `teacher` и `student`; внешний доступ только через gateway.
- Создание ученика преподавателем и связь teacher-student.
- Расписание, создание занятий, завершение и отмена занятий.
- Материалы занятий: `lesson-service` хранит `file_id`, сами файлы лежат в `file-service`.
- Домашние задания, файлы к ДЗ, сдача решения, review, комментарии.
- Загрузка файлов и скачивание через gateway/file-service.
- Payment receipts: ученик загружает чек, преподаватель подтверждает или отклоняет.
- Финансовый append-only журнал: `charge`, `payment`, корректирующие операции.
- Асинхронное создание charge: `lesson.completed` через transactional outbox -> Kafka -> finance consumer.
- Notification-service: Kafka consumer создаёт in-app уведомления; frontend показывает список и mark-as-read.
- React + TypeScript + Vite frontend для teacher/student demo-flow.
- Smoke и pytest e2e-проверки через внешний gateway.

## Архитектура

```text
frontend (React/Vite)
        |
        | REST/JSON + multipart
        v
api-gateway :8080  -- единственный публичный backend endpoint
        |
        | gRPC для синхронных доменных вызовов
        v
identity-service   :9081 / identity_db
lesson-service     :9082 / lesson_db
assignment-service :9083 / assignment_db
finance-service    :9084 / finance_db
notification-service :9086 / notification_db

api-gateway -- HTTP multipart --> file-service :8085 / file_db

lesson-service -- transactional outbox --> Kafka topic tutorflow.lesson.completed
Kafka --> finance-service consumer --> charge в finance_db
assignment/finance/lesson events --> Kafka --> notification-service --> notifications
```

Основные правила архитектуры:

- Внешние клиенты ходят только в `api-gateway`.
- У каждого сервиса своя БД; чужие БД не читаем.
- Синхронные внутренние операции идут по gRPC.
- Файлы остаются на HTTP multipart, потому что это проще и практичнее для upload/download.
- События и побочные эффекты идут через Kafka. Первый внедренный flow:
  `lesson.completed -> finance charge`.
- In-app уведомления строятся асинхронно из Kafka-событий и читаются через gateway.
- Ошибки наружу возвращаются в едином envelope:

```json
{"error":{"code":"...","message":"...","details":null}}
```

## Сервисы

| Сервис | Назначение | Внешний порт | Внутренний gRPC | БД |
|---|---|---:|---:|---|
| `api-gateway` | auth, routing, REST API для frontend | `8080` | — | — |
| `identity-service` | пользователи, JWT, связи teacher-student | — | `9081` | `identity_db` |
| `lesson-service` | расписание, занятия, outbox `lesson.completed` | — | `9082` | `lesson_db` |
| `assignment-service` | ДЗ, submissions, review, comments | — | `9083` | `assignment_db` |
| `finance-service` | receipts, баланс, append-only ledger, Kafka consumer | — | `9084` | `finance_db` |
| `file-service` | metadata + локальное хранение файлов | — | — | `file_db` |
| `notification-service` | in-app уведомления из Kafka-событий | — | `9086` | `notification_db` |

## Библиотеки

- `libs/common` — инфраструктура: ошибки, auth context, JWT, HTTP helpers, handler helpers.
- `libs/proto` — protobuf/gRPC контракты `*.v1` и userver codegen.
- `libs/clients` — общие gRPC clients, deadlines/retries/metadata/error mapping.
- `libs/events` — Kafka envelope, publisher и consumer helpers.

## Поток Complete Lesson И Charge

Завершение занятия теперь честно асинхронное:

1. Teacher вызывает `POST /lessons/{id}/complete` через gateway.
2. Gateway вызывает `lesson-service` по gRPC.
3. `lesson-service` в одной DB-транзакции:
   - переводит lesson в `completed`;
   - пишет `lesson.completed` в `outbox_events`.
4. Ответ наружу: HTTP `200` и JSON:

```json
{
  "lesson": {"status": "completed"},
  "charge_status": "pending"
}
```

5. `lesson-outbox-publisher` публикует событие в Kafka.
6. `finance-lesson-completed-consumer` создает `charge`.
7. Идемпотентность держится в finance DB по `unique(lesson_id)`: повтор complete
   или replay события не создают второй charge.

## Быстрый Старт

```bash
cp .env.example .env
COMPOSE_PARALLEL_LIMIT=1 docker compose build
docker compose up -d
curl http://localhost:8080/health
```

Миграции применяются автоматически one-shot сервисом `migrator`. Ручной прогон:

```bash
./scripts/migrate.sh all
```

Сброс dev-БД:

```bash
docker compose down -v
```

## Frontend

```bash
cd frontend
npm install
npm run dev
```

Frontend по умолчанию доступен на:

```text
http://localhost:5173/
```

Gateway должен быть доступен на `http://localhost:8080`. CORS по умолчанию
настроен на `http://localhost:5173`.

Production build frontend:

```bash
cd frontend
npx vite build
```

## Проверки

Полный smoke через внешний gateway:

```bash
python3 scripts/smoke_mvp.py
```

Pytest e2e:

```bash
python3 -m pytest tests
```

Frontend build:

```bash
cd frontend
npx vite build
```

`scripts/smoke_mvp.py` и finance-тесты учитывают eventual consistency: после
complete они ждут появления charge/обновления баланса через poll-with-timeout.

## Demo Данные

В проекте есть helper для заполнения демо-данных:

```bash
python3 scripts/demo_seed.py
```

Он предназначен для локальной проверки UI через gateway.

## Сборка И OOM

userver/C++ translation units тяжелые, а generated gRPC code заметно увеличивает
память на сборке. Для обычной локальной машины безопаснее собирать последовательно:

```bash
COMPOSE_PARALLEL_LIMIT=1 docker compose build
```

Если Docker все равно ловит `cc1plus killed`, собирайте сервисы по одному:

```bash
COMPOSE_PARALLEL_LIMIT=1 docker compose build identity-service
COMPOSE_PARALLEL_LIMIT=1 docker compose build lesson-service
COMPOSE_PARALLEL_LIMIT=1 docker compose build assignment-service
COMPOSE_PARALLEL_LIMIT=1 docker compose build finance-service
COMPOSE_PARALLEL_LIMIT=1 docker compose build file-service
COMPOSE_PARALLEL_LIMIT=1 docker compose build api-gateway
```

## Структура Репозитория

```text
services/                 C++ userver services
  api-gateway/
  identity-service/
  lesson-service/
  assignment-service/
  finance-service/
  file-service/
  notification-service/
libs/
  common/                 common infra helpers
  clients/                shared gRPC clients
  proto/                  protobuf/gRPC contracts and codegen
  events/                 Kafka event helpers
migrations/               SQL migrations per service
frontend/                 React + TypeScript + Vite UI
scripts/                  smoke, migrations, demo seed
docker/postgres/initdb    per-service DB bootstrap
docs/                     architecture notes and contracts
```

## Что Не Входит Сейчас

- Реальные платежные интеграции.
- Redis, чат, report-service.
- Email/Telegram/push-уведомления: сейчас есть только in-app notification-service.
- S3/MinIO storage вместо локального file storage.

Эти направления оставлены как следующие этапы развития.
