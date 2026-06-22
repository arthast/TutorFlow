# TutorFlow — Roadmap (общий, для любого агента)

> Очередь работ. Привязки «сервис → агент» нет: исполнителя на каждую задачу
> выбирает человек, включая нужного агента (Claude или Codex) поочерёдно.
> Координатор — Claude (контракты, PLAN, ревью, интеграция). Источники правды:
> `docs/PLAN.md`, `docs/api-contracts/*.openapi.yaml`, общие правила — `AGENTS.md`.
>
> Стадия проекта: **не «архитектура на бумаге», а стабилизация MVP.** Новые
> сервисы сейчас НЕ добавляем. Цель этапа — довести 6 готовых сервисов до
> демонстрируемого продукта: сквозной сценарий + тесты + простой UI + деплой.

Обновлено: 2026-06-22.

---

## Статус

| Сервис | Состояние |
|---|---|
| `libs/common` | каркас готов (errors, auth_context, http_client, jwt) |
| identity-service | реализован, в `main` |
| file-service | реализован, в `main` |
| lesson-service | реализован, в `main` |
| assignment-service | реализован, в `main` |
| finance-service | реализован, в `main` |
| api-gateway | **реализован** (JWT локально, срез/постановка X-User-*, проксирование), в `main` |

Ядро из 6 сервисов собрано и согласовано. Дальше — закрыть gaps до полного
сценария, тесты, фронт, деплой.

---

## Текущее межсервисное взаимодействие (зафиксировано по коду)

Транспорт: синхронный REST/HTTP + JSON (`libs/common/http_client_base` поверх
userver http-client, base-url из env `<SVC>_SERVICE_URL`). Пробрасываются
`X-User-Id`, `X-User-Roles`, `X-Request-Id`. Чужую БД никто не читает. Kafka нет.

**Что реально вызывается сейчас:**
```text
gateway    → identity, lesson, assignment, finance, file   (проксирование + auth)
lesson     → identity   (check-access перед созданием занятия)
lesson     → finance    (POST /internal/charges при complete, идемпотентно)
assignment → identity   (check-access перед созданием ДЗ)
finance    → identity   (check-access)
file       → identity   (check-access на скачивание)
identity   → никого     (лист графа)
```

**Разрешено, но пока НЕ подключено** (завести client-интерфейс при реальной
надобности, не раньше): `assignment → file`, `finance → file`. Связь
`finance → lesson` намеренно отсутствует — стрелка развёрнута: lesson сам пушит
`charge` в finance. (NB: в PLAN §10 граф описан как «разрешённый» — при правке
доков, Этап 1.3, привести §10 к этому разделению «реально / разрешено».)

**Будущее (после MVP, не сейчас):** побочные действия вынести в события
(`lesson_completed`, `assignment_created`, `submission_uploaded`,
`assignment_reviewed`, `payment_receipt_uploaded`, `payment_confirmed`) через
Kafka/outbox; слушатели — notification/report. Синхронными остаются только
вызовы, где нужен немедленный ответ (проверка прав, получение файла/метаданных).

---

## Этап 1 — Stabilize MVP (приоритет)

Цель: полный сценарий из PLAN §2 реально проходит через gateway.

### 1.1 Temp-password flow для ученика  ✅ СДЕЛАНО (ревью пройдено, ждёт мерджа)
Реализовано в identity + gateway: `POST /students` требует email+temp password,
связь создаётся `active`; добавлен `POST /auth/change-password`. См.
`docs/agent-identity-temp-password.md`.
Остался мелкий edge-case → задача **1.6**.

Исходный контекст и flow (без invite-токенов):
```text
преподаватель и ученик связываются в другой соцсети (вне системы)
ученик присылает преподавателю свою почту
преподаватель создаёт аккаунт: реальный email + временный пароль
ученик логинится по email + временный пароль
ученик меняет пароль
```
Эндпоинты (public, через gateway):
```text
POST /students            (есть) — теперь требует email + password (временный)
POST /auth/login          (есть) — ученик входит по email + temp password
POST /auth/change-password (новый, нужен Bearer) — body { current_password, new_password }
```
Внутри identity: `CreateStudent` сохраняет реальный email + хеш временного пароля
(PBKDF2 как в register), связь создаётся в статусе `active`; новый internal
`POST /internal/auth/change-password` (по X-User-Id) меняет `password_hash`.
Новой таблицы/миграции не требуется. **Контракт меняется** (identity + gateway
OpenAPI) → согласовано. DoD: ученик логинится по temp-паролю, меняет пароль,
выполняет submit/upload под собой.

### 1.2 Проверить hourly_rate в lesson-service
identity отдаёт `check-access → {allowed, status, hourly_rate}` (в `main`).
Убедиться, что lesson при `CreateLessonRequest.price == null` снимает
`lessons.price` из `hourly_rate`; временный fallback `422 business_rule`
оставить только когда и price, и hourly_rate пусты. Снять раздел «Known Contract
Gap» из `docs/agent-b-lesson-service.md`, если ещё не снят.

### 1.3 Привести docs к реальным контрактам
- identity: проверка связи — `/internal/relations/check-access` (не
  `/teacher-student-links/check`).
- file-service: поле `purpose` (не `resource_type`/`resource_id`); `resource_id`
  принимается, но не хранится — отразить в доке/контракте.
- PLAN §10: развести «реально вызывается» и «разрешено» (см. раздел выше).

### 1.4 CORS в api-gateway (до фронта)  ✅ СДЕЛАНО
Без CORS браузер не сможет ходить в gateway. Добавить:
```text
Access-Control-Allow-Origin      (из env, напр. http://localhost:5173)
Access-Control-Allow-Methods
Access-Control-Allow-Headers      (Authorization, Content-Type, X-Request-Id)
обработка OPTIONS preflight
```
Origin брать из env (dev: `http://localhost:5173`, prod: домен). **Не `*`** при
авторизации. Бизнес-логику в gateway не добавлять — только заголовки/preflight.

### 1.5 Сквозной smoke-тест `scripts/smoke_mvp.py`  ✅ СДЕЛАНО (SMOKE OK)
Python (удобнее с JSON/токенами/файлами). Гоняет весь сценарий через gateway:
```text
1 register teacher              6 teacher creates lesson        11 finance creates charge
2 login teacher                 7 teacher creates assignment    12 student uploads receipt
3 create student (email+temp)   8 student submits solution      13 teacher confirms receipt
4 student login(temp)+change pw 9 teacher reviews assignment    14 finance creates payment
5 student re-login (new pw)     10 teacher completes lesson      15 balance is updated
```
DoD: скрипт проходит 15/15 на чистой `docker compose up`.

### 1.6 Дубль email при создании ученика → 409 (мелкая доработка)
Сейчас `POST /students` с уже существующим email упирается в `users.email UNIQUE`
и, вероятно, возвращает 500. Поймать `unique_violation` в identity-репозитории и
отдавать `409 conflict` в едином envelope (код `email_taken`). Добавить `409` в
`identity`/`gateway` OpenAPI для `/students` (правка контракта — через
координатора). Только identity + gateway.

### 1.7 Receipt: student_id из X-User-Id (фикс разрыва на шаге 12 smoke)  ✅ СДЕЛАНО
Найдено в 1.5: finance `POST /internal/payment-receipts` требовал `teacher_id` и
`student_id` в теле, а публичный gateway-контракт их не содержал → 400.
Согласованное решение (контракты уже обновлены координатором):
- `student_id` берётся из `X-User-Id` (аутентифицированный ученик), НЕ из тела;
- `teacher_id` остаётся в теле (у ученика может быть несколько преподавателей).
Сделать в finance-service: при создании чека читать `student_id` из `X-User-Id`,
`teacher_id` — из тела; перестать требовать `student_id` в теле. Обновить
`scripts/smoke_mvp.py` (шаг 12 шлёт `teacher_id`). Только finance-service (+ smoke).

**Результат Этапа 1:** ✅ ДОСТИГНУТ — полный 15-шаговый сценарий проходит через
gateway end-to-end (`scripts/smoke_mvp.py` → SMOKE OK). Остаются мелкие
доработки 1.2 (проверить hourly_rate), 1.3 (выровнять доки), 1.6 (дубль email →
409) — не блокируют e2e, можно делать попутно.

---

## Этап 2 — Tests

Сначала один e2e (Этап 1.5), потом негативные/бизнес-проверки. Не писать 100
unit-тестов раньше времени — для микросервисного MVP важнее, что сценарий проходит.

Негативные проверки (минимум):
```text
1 нельзя подделать X-User-Id / X-User-Roles извне (gateway срезает)
2 ученик не видит чужие ДЗ
3 преподаватель не работает с чужим учеником (check-access)
4 повторный complete не создаёт второй charge (идемпотентность)
5 загрузка чека не меняет баланс
6 баланс меняется только после confirm receipt
7 rejected receipt не создаёт payment
```
Структура:
```text
tests/
  smoke/        smoke_mvp.py
  integration/  test_auth.py  test_lessons.py  test_assignments.py  test_finance.py
```

---

## Этап 3 — Simple frontend

Цель — показать сценарий, не идеальный UI. Стек: **React + TypeScript + Vite**,
обычные формы без дизайн-системы.
```text
frontend/   (React + TS + Vite)
```
Минимум экранов/функций:
```text
login / register
Teacher dashboard:  список учеников, создать ученика, создать занятие,
                    создать ДЗ, посмотреть решения, проверить, завершить занятие,
                    посмотреть чеки, подтвердить чек
Student dashboard:  список занятий, список ДЗ, сдать решение, загрузить чек, баланс
```
Зависит от: 1.4 (CORS). Все запросы — только в gateway (:8080).

---

## Этап 4 — Deploy

После smoke-теста и CORS. Наружу доступны только `frontend` и `api-gateway`;
внутренние сервисы и postgres — нет.
```text
Caddy :80/:443 → frontend (static) + api-gateway → internal services → PostgreSQL
```
Артефакты:
```text
docker-compose.prod.yml
Caddyfile
.env.prod.example
volumes:  pgdata, filestorage
healthchecks для сервисов
README_DEPLOY.md
```
Порядок: собрать образы → `docker compose up` на сервере → Caddy → env secrets →
поднять БД и сервисы → прогнать `smoke_mvp.py` против prod/staging URL.

---

## Этап 5 — Future services (только после деплоя)

Не превращать систему в граф «каждый зовёт каждого». Порядок добавления:
```text
1 Kafka / outbox (вынести события из синхронных вызовов)
2 notification-service   (первый: хорошо ложится на события, продуктово полезен,
                          показывает Kafka/outbox, проще чата)
3 report-service         (read-модели для dashboard из событий)
4 chat-service
5 Redis                  (чат, кэш, online-state)
6 MinIO/S3               (вместо локального file storage)
```
identity позже можно разделить на `auth-service` + `user-service`. Прямой вызов
`lesson → finance` при желании заменить на `lesson → Kafka → finance`.

---

## Правила назначения задач
- В один момент активен **один** агент с **одной** задачей (PLAN §13).
- Человек называет агенту задачу по номеру (напр. «Этап 1.1»).
- Зависимости: 1.5 требует 1.1; Этап 3 требует 1.4; Этап 4 требует Этапа 2 (smoke).
- Менять контракты (api-contracts) и public-сигнатуры `libs/common` — только через
  координатора (Claude) с подтверждением человека. Задачи 1.1 и 1.4 затрагивают
  контракты gateway/identity — согласовать ДО реализации.
