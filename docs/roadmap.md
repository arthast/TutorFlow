# TutorFlow — Roadmap (общий, для любого агента)

> Очередь работ. Привязки «сервис → агент» нет: исполнителя на каждую задачу
> выбирает человек, включая нужного агента (Claude или Codex) поочерёдно.
> Координатор — Claude (контракты, PLAN, ревью, интеграция). Источники правды:
> `docs/PLAN.md`, `docs/api-contracts/*.openapi.yaml`, общие правила — `AGENTS.md`.
>
> Стадия проекта: MVP-ядро стабилизировано; следующий архитектурный фокус —
> аккуратно расширять уже работающую gRPC/Kafka базу. Команды и запросы остаются
> синхронными через gRPC, Kafka используется только для доменных фактов и
> побочных эффектов.

Обновлено: 2026-06-24. (Восстановлено из git + дописан статус Этапа 5 после потери
локальных docs при пересоздании ветки.)

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
| api-gateway | **реализован** (JWT локально, срез/постановка X-User-*, проксирование), в `main`; в ветке 5G добавлены notification routes |
| notification-service | реализуется в ветке `feat/notification-service`: Kafka consumer + gRPC list/mark-read |

Ядро из 6 сервисов собрано и согласовано: REST снаружи через gateway, gRPC между
сервисами, Kafka/outbox для первого бизнес-flow `lesson.completed -> charge`.
Дальше — закрыть UI gaps и расширять event-driven слой вокруг уже работающего ядра.

---

## Текущее межсервисное взаимодействие (зафиксировано по коду)

Транспорт: REST/JSON снаружи (`frontend -> api-gateway`) и HTTP multipart для
файлов (`gateway -> file-service`); gRPC для синхронных внутренних вызовов;
Kafka для асинхронных доменных событий. Пробрасываются `X-User-Id`,
`X-User-Roles`, `x-request-id`/`x-trace-id`. Чужую БД никто не читает.

**Что реально вызывается сейчас:**
```text
gateway    → identity, lesson, assignment, finance   (gRPC + auth/mapping)
gateway    → file                                    (HTTP multipart upload/download)
lesson     → identity                                (gRPC check-access)
assignment → identity                                (gRPC check-access)
finance    → identity                                (gRPC check-access)
file       → identity                                (gRPC check-access на скачивание)
lesson     → Kafka                                   (`lesson.completed` через outbox)
finance    ← Kafka                                   (consumer создаёт charge)
notification ← Kafka                                 (`assignment.*`, `payment.*`, `lesson.completed`)
identity   → никого                                  (лист графа)
```

**Разрешено, но пока НЕ подключено** (завести client-интерфейс при реальной
надобности, не раньше): `assignment → file`, `finance → file`.

**Будущее:** расширять Kafka только под понятных потребителей. Ближайшие события:
`assignment.created`, `submission.uploaded`, `assignment.reviewed`,
`payment_receipt.uploaded`, `payment.confirmed`, `payment.rejected`,
`charge.created`, `balance.changed`. Потенциальные слушатели:
notification-service, report-service, audit/read-models. Синхронными остаются
операции, где нужен ответ сейчас: auth, access-check, create/get/review/confirm,
balance, file upload/download.

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

### 1.2 Проверить hourly_rate в lesson-service  ✅ СДЕЛАНО
identity отдаёт `check-access → {allowed, status, hourly_rate}` (в `main`).
Убедиться, что lesson при `CreateLessonRequest.price == null` снимает
`lessons.price` из `hourly_rate`; временный fallback `422 business_rule`
оставить только когда и price, и hourly_rate пусты. Снять раздел «Known Contract
Gap» из `docs/agent-b-lesson-service.md`, если ещё не снят.

### 1.3 Привести docs к реальным контрактам  ✅ СДЕЛАНО
(check-access и `purpose` уже консистентны; PLAN §10 разведён на «реально/разрешено».)

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

### 1.6 Дубль email при создании ученика → 409 (мелкая доработка)  ✅ СДЕЛАНО
Контракты УЖЕ обновлены координатором (`409` добавлен в `/students` в identity +
gateway OpenAPI). Осталась реализация в identity: поймать `unique_violation` в
репозитории и вернуть `409` в едином envelope (код `email_taken`). Только
identity-service.

### 1.7 Receipt: student_id из X-User-Id (фикс разрыва на шаге 12 smoke)  ✅ СДЕЛАНО
Найдено в 1.5: finance `POST /internal/payment-receipts` требовал `teacher_id` и
`student_id` в теле, а публичный gateway-контракт их не содержал → 400.
Согласованное решение (контракты уже обновлены координатором):
- `student_id` берётся из `X-User-Id` (аутентифицированный ученик), НЕ из тела;
- `teacher_id` остаётся в теле (у ученика может быть несколько преподавателей).
Сделать в finance-service: при создании чека читать `student_id` из `X-User-Id`,
`teacher_id` — из тела; перестать требовать `student_id` в теле. Обновить
`scripts/smoke_mvp.py` (шаг 12 шлёт `teacher_id`). Только finance-service (+ smoke).

**Результат Этапа 1:** ✅ ПОЛНОСТЬЮ ЗАКРЫТ — e2e зелёный (`scripts/smoke_mvp.py`
→ SMOKE OK), все доработки 1.2–1.7 сделаны. Доп. бонус: assignment `GetAssignment`
теперь проверяет участника (`EnsureParticipant`) — закрыт кейс «чужой ДЗ по id».

---

## Этап 2 — Tests  ✅ СДЕЛАНО (ждёт код-ревью координатора)

Реализованы: health, негативный auth (+защита от подделки X-User-*), контроль
доступа (чужой ученик/чужие ДЗ), идемпотентность charge и правила баланса/чеков.
Гайд: `docs/testing-guide-stage2.md`.

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

## Рефактор R1 — вынос общего в libs (перед 2.5)

Чистый механический рефактор, без изменения поведения и контрактов. Одной
сфокусированной задачей в одну сессию (трогает libs + сервисы).

**A. handler-хелперы → `libs/common/handler_helpers.hpp`** (чистая инфра):
`JsonResponse`, `ErrorResponse`, `HandleEnvelope`, `ParseJsonBody`, `RequireString`,
`OptionalString`, `RequireDouble`, `OptionalDouble` — сейчас продублированы в
анонимных namespace каждого сервиса. Вынести в общий заголовок, убрать локальные
копии. Сервисы: identity, lesson, assignment, finance, file. **gateway — НЕ в этот
заход** (у него response-обёртки завязаны на CORS; адаптируем отдельно при желании).

**B. `IdentityClient` → новый `libs/clients`** (нельзя в common — несёт
identity-DTO, правило §7): `tutorflow::clients::{AccessCheckResult, IdentityClient,
HttpIdentityClient}`. `AccessCheckResult{allowed, status, hourly_rate?}`. Компонент
оставить под именем `identity-client` (env `IDENTITY_SERVICE_URL`) → `static_config`
сервисов не меняется. lesson/assignment/finance/file удаляют локальные
`clients/identity_client.*` и используют общий; file переключается с `-> bool` на
`.allowed`.

DoD: все 6 сервисов собираются (`docker compose build`); `smoke_mvp.py` → SMOKE OK;
тесты Stage 2 зелёные; локальных копий identity_client/handler-хелперов в 5
сервисах не осталось; поведение и контракты не изменились.

---

## Этап 2.5 — Demo-ready backend

Фокус: не расширять архитектуру, а сделать текущие 6 сервисов проверяемым и
демонстрируемым продуктом. Делается после закрытия 1.2/1.6.

### 2.5.1 Top-up тестов (недостающие edge-cases поверх Этапа 2)
Добавить именно недостающее, не пачку ради объёма:
- **finance — state-machine чека** (поведение зафиксировано в контракте, см. ниже):
  - double confirm уже `confirmed` → `200` с текущим состоянием, второй `payment`
    НЕ создаётся, баланс меняется один раз (истинная идемпотентность);
  - confirm уже `rejected` чека → `409`; reject уже `confirmed` чека → `409`.
- **assignment**: `student` не может `review`; `student` не может создать
  assignment; `teacher` не может `review` чужую assignment. (Комментарии НЕ
  ограничиваем — по бизнес-логике комментировать могут и teacher, и student.)
- **lesson**: `student` не может создать lesson; `student` не может `complete`;
  `teacher` не может `complete` чужой lesson.
- **gateway**: клиент шлёт `X-User-Id` другого пользователя → gateway игнорирует
  и подставляет id из JWT (явный security-кейс).

### 2.5.2 Comment-шаг в smoke_mvp.py
Добавить `POST /assignments/{id}/comments` после review — чтобы smoke покрывал
весь сценарий из описания MVP (review + leave comment).

### 2.5.3 README quickstart
README = инструкция «как с нуля запустить и увидеть, что работает». Структура:
```text
# TutorFlow
## Что это
## Что реализовано в MVP
## Архитектура
## Сервисы
## Как запустить        (docker compose up --build)
## Переменные окружения
## Миграции             (./scripts/migrate.sh all)
## Smoke test           (python3 scripts/smoke_mvp.py → SMOKE OK = MVP flow работает)
## Demo flow
## API contract         (docs/api-contracts/gateway.openapi.yaml)
## Что не входит в MVP
## Roadmap
```

### 2.5.4 Сверить gateway OpenAPI с реализацией
OpenAPI уже есть — НЕ создаём заново, а проверяем соответствие:
все актуальные эндпоинты присутствуют; нет несуществующих; error envelope описан
как `{error:{code,message,details}}`; bearer-auth описан; роли указаны в
description. Swagger UI сейчас НЕ делаем (косметика).

> Контрактная фиксация (сделано координатором): в `finance`/`gateway` OpenAPI у
> `confirm`/`reject` добавлен `409`; идемпотентный повтор того же действия → `200`
> с текущим состоянием; смена финального решения (confirm↔reject) → `409`.

---

### 2.5.5 gateway: публиковать GET /payments/receipts (пре-реквизит фронта)
finance умеет отдавать список чеков (`GET /internal/payment-receipts`, скоуп по
X-User-Id, фильтр `?status=`), но gateway его не публикует — наружу только POST.
Без этого преподаватель в UI не увидит чеки на подтверждение. Контракт уже
обновлён координатором (добавлен `GET /payments/receipts`). Сделать в gateway:
разрешить `GET` на `/payments/receipts` и проксировать в finance
`/internal/payment-receipts` (query `?status=` прокидывать). Только api-gateway.

### 2.5.6 finance: контроль доступа на balance/transactions (IDOR-фикс)  ✅ СДЕЛАНО
Реализовано: `EnsureStudentAccess` (сам ученик или его преподаватель через
identity check-access; иначе `403`).

Сейчас `GET /internal/students/{id}/balance` и `.../transactions` в finance берут
id из пути и НЕ проверяют зовущего — любой залогиненный может прочитать чужой
баланс. Закрыть: разрешать только если зовущий — сам ученик
(`X-User-Id == studentId`) ИЛИ его преподаватель (identity `check-access(teacher=
caller, student=studentId).allowed`); иначе `403`. Только finance-service
(в handler'ах balance/transactions распарсить auth и добавить проверку; для
teacher-кейса использовать уже имеющийся identity-client). Контракт без изменений
(добавляется лишь `403`). Нужно для безопасного показа долга ученику (фронт уже
зовёт `GET /students/{me}/balance`).

### 2.5.7 finance: student видит свои чеки в GET /internal/payment-receipts  ✅ СДЕЛАНО
Реализовано: `ListReceipts` скоупит по роли — `ListReceiptsForTeacher` /
`ListReceiptsForStudent`, фильтр `?status=` сохранён.

После 2.5.5 gateway публикует `GET /payments/receipts`, но finance `ListReceipts`
фильтрует только по `teacher_id = X-User-Id` — ученик получает `200 []` вместо
своих чеков (нужно фронту: «my receipts со статусами»). Закрыть в finance-service:
скоупить список по роли зовущего — teacher видит чеки где `teacher_id = X-User-Id`,
student видит чеки где `student_id = X-User-Id` (роль из `X-User-Roles`). Фильтр
`?status=` сохранить. Только finance-service. Контракт менять не требуется (summary
`/internal/payment-receipts` GET можно уточнить координатору, что список скоупится
по роли). Зависит от 2.5.5 (готово).

---

## Этап 3 — Frontend MVP (ролевые дашборды)

Цель — ролевой demo-UI под готовый backend, не идеальный продукт. Стек:
**React + TypeScript + Vite**, обычные формы. Все запросы — ТОЛЬКО в gateway
(:8080). Зависит от 1.4 (CORS, готово).
```text
frontend/   (React + TS + Vite)
```
Teacher dashboard: login/register, students list, create student, lessons list,
create lesson, complete lesson, assignments list, create assignment, review
submission, receipts list, confirm/reject receipt, student balance.
Student dashboard: login/register, my lessons, my assignments, submit assignment,
upload receipt (заявить сумму + файл), my receipts со статусами, change password.
**У ученика НЕТ баланса** — проект не про пополняемый кошелёк: ученик заявляет
оплату + чек, преподаватель подтверждает. Баланс/долг — учётная величина только
на стороне преподавателя.

---

## Этап 3.6 — Файлы: вложения ДЗ, чеки, материалы урока

### 3.6.0 file-service: симметричный доступ student↔teacher  [backend]  ✅ СДЕЛАНО
Реализовано: при не-владельце и не-teacher проверяется обратная ветка
`check-access(owner, requester)`; иначе `403`. (file→identity check-access — gRPC, 5C.)

Сейчас `Download` пускает только владельца и преподавателя-владельца-ученика
(`check-access(requester_teacher, owner_student)`). Значит ученик НЕ может скачать
файл, загруженный преподавателем (вложение ДЗ, материалы урока) → 403. Добавить
обратную ветку: если зовущий НЕ владелец и НЕ teacher, проверить
`check-access(owner, requester)` — т.е. владелец-преподаватель связан с
учеником-зовущим. Иначе 403. Только file-service. Контракт без изменений.
Нужно для ученической стороны 3.6.1 и для 3.6.3.

### 3.6.1 Вложения ДЗ и решений во фронте  ✅ ФРОНТ ГОТОВ (backend готов; ученическое скачивание teacher-файлов — после 3.6.0)
Бэкенд уже хранит и отдаёт `file_ids` (assignment_files/submission_files, в
контракте и в AssignmentDetail/Submission). Нужно только в UI:
- teacher при создании ДЗ прикрепляет файл(ы): для каждого `POST /files`
  (purpose=assignment_attachment) → собрать `file_ids` → `POST /assignments {file_ids}`;
- в детали ДЗ показывать вложения (file_ids) с именами (`GET /files/{id}` →
  original_name) и кнопкой открыть/скачать (`openFile`) — видно и teacher, и student
  (закрывает «преподаватель не может посмотреть файл ДЗ» и доступ ученика к ним);
- student при сдаче может прикрепить НЕСКОЛЬКО файлов (сейчас один — расширить);
- teacher видит и открывает файлы решения ученика (submission.file_ids).

### 3.6.2 Просмотр своих чеков и их файлов учеником  ✅ ФРОНТ ГОТОВ
В «Мои чеки» (уже есть список со статусами) добавить кнопку открыть свой файл
чека (`openFile`, ученик — владелец файла). История чеков уже отображается.

### 3.6.3 Материалы урока (lesson materials)  [backend ✅ / frontend ⬜]
- Backend (lesson-service): ✅ СДЕЛАНО — миграция `002_lesson_files.sql` (`lesson_files`),
  `file_ids` в `CreateLessonRequest`/`Lesson`; `lesson_material` в enum `purpose`
  file-service (`002_lesson_material_purpose.sql`).
- Frontend: ⬜ teacher грузит материалы к занятию; teacher и student видят/скачивают.
  (В работе — текущий заход по фронту.)

### Backlog (не в MVP)
- **Загрузка папок** — file-service хранит плоские файлы; папки потребуют zip-архива
  или path-метаданных. Множественные файлы поддержаны (несколько `POST /files`).

---

> Этапы 4 (frontend polishing) и 4.5 (deploy) отложены — перешли сразу к Этапу 5.
> (Frontend polishing частично закрывается в текущем заходе по фронту: материалы
> урока 3.6.3 + UX-доводка.)

---

## Этап 5 — Расширение архитектуры (REST → gRPC → Kafka)

> Гейт: gRPC-трек (5A–5C) сделан; Kafka-трек (5D–5E) сделан; дальше 5F
> (event hardening + события), 5G notification, 5H report, 5I storage, 5J chat.
> Все proto/event-контракты — через координатора (Claude).

**Главный принцип: gRPC сначала, Kafka потом.** Разделение коммуникаций по смыслу:
```text
REST  — внешний API: frontend → api-gateway (+ файлы gateway → file-service, multipart)
gRPC  — синхронные внутренние вызовы (service↔service, gateway→service)
Kafka — асинхронные доменные события и побочные эффекты
```
Правило выбора: нужен ответ сейчас (check-access, get/create, review, confirm,
balance) → gRPC; побочный эффект (lesson.completed → charge) → Kafka. Access-check
Kafka'ой НЕ делать; gateway-команды через Kafka НЕ слать.

### Этап 5A — gRPC foundation  ✅ СДЕЛАНО
`userver COMPONENTS … grpc`; `libs/proto` (`common/identity/lesson/assignment/finance`,
`*.v1`) через `userver_add_grpc_library`; ошибки только через gRPC status codes;
общий `libs/clients/grpc_client_base` (deadline, retries только idempotent, метаданные
`X-User-Id`/`X-User-Roles`/`x-request-id`/`x-trace-id`, маппинг `grpc::Status →
ServiceError`); gRPC health (9081..9084). См. `docs/agent-grpc-foundation.md`.
NB: полный `docker compose build` упирался в память (OOM) — закрыто на этапе
операционной закалки (`BUILD_JOBS`).

### Этап 5B — identity на gRPC  ✅ СДЕЛАНО
gRPC `IdentityService` (9 методов, включая добавленный координатором `ChangePassword`);
gateway ходит в identity по gRPC (auth/user/students), внешний REST не изменился;
`email_taken → ALREADY_EXISTS`; lesson/assignment/finance переключены на gRPC
check-access. См. `docs/agent-identity-grpc.md`.

### Этап 5C — domain services на gRPC + зачистка/DRY  ✅ СДЕЛАНО
gRPC-серверы Lesson/Assignment/Finance (9082/9083/9084); gateway вызывает их по gRPC;
внешний REST не менялся. Зачистка: общий gRPC-код в `libs/clients`
(`grpc_server_utils` + расширенный `grpc_client_base`), удалён мёртвый HTTP
`HttpIdentityClient` и deprecated internal REST identity/lesson/assignment/finance;
`file-service` check-access переведён на gRPC. Оставшийся внутренний REST: только
файловые multipart-вызовы. См. `docs/agent-domain-grpc.md`.

### Этап 5D — Kafka foundation  ✅ СДЕЛАНО
Брокер `kafka` (apache/kafka:3.8.1, KRaft, внутренний, healthcheck, volume);
`libs/events` (`tutorflow_events`) на `userver::kafka`: `event_envelope`/`event_publisher`/
`event_consumer`; `docs/event-contracts/` (versioned JSON). Envelope `{event_id,
event_type, event_version, occurred_at, producer, trace_id, payload}`. Брокеры через
secdist. См. `docs/agent-kafka-foundation.md`.

### Этап 5E — transactional outbox + первый бизнес-flow  ✅ СДЕЛАНО
**5E-1:** `migrations/lesson/003_outbox_events.sql`; `CompleteLesson` пишет статус +
`lesson.completed` в outbox одним CTE (атомарно, повтор не пишет второй outbox);
publisher `lesson-outbox-publisher` (PeriodicTask, at-least-once); consumer
`finance-lesson-completed-consumer` создаёт charge идемпотентно по `unique(lesson_id)`.
**5E-2 (cutover, вариант A):** прямой `lesson→finance` вызов и REST `/internal/charges`
удалены — charge создаётся ТОЛЬКО из события; контракт (координатор) `CompleteLesson
→ CompleteLessonResponse{lesson, charge_status:"pending"}`; фронт дообновляет баланс;
smoke/`test_finance` на poll-with-timeout. Реплей/повтор второй charge не создаёт.
См. `docs/agent-outbox-lesson-completed.md`.

### Этап 5F — event foundation hardening + новые доменные события  ✅ СДЕЛАНО (5F-0/1/2)
Цель: превратить Kafka-flow из разового `lesson.completed` в переиспользуемый
event-driven каркас. Не добавлять события ради событий: у каждого события должен
быть ожидаемый потребитель (notification/report/audit/read-model).

**5F-0. Hardening событийной базы — ✅ СДЕЛАНО**
- Зафиксировать naming convention: `<domain>.<past_tense>` (`assignment.created`,
  `payment.confirmed`, `balance.changed`).
- Расширить `docs/event-contracts/`: versioned JSON-контракты для новых событий.
- Вынесен общий outbox publisher в `libs/events`.
- Добавлен consumer idempotency/inbox (`processed_events(event_id primary key,
  event_type, processed_at)`) для finance consumer `lesson.completed`.
- Зафиксировать retry/DLQ convention: retry только безопасных обработчиков,
  DLQ topic naming, structured logs, request/trace id в event envelope.
- Минимальное наблюдение сейчас через structured logs; полноценный DLQ/lag
  monitoring остаётся в production hardening (5K).

**5F-1. `assignment-service -> Kafka` — ✅ СДЕЛАНО**
Через outbox публиковать:
```text
assignment.created       -> notification-service, report-service
submission.uploaded      -> notification-service, report-service
assignment.reviewed      -> notification-service, report-service
```
Опционально позже: `assignment.needs_fix`, `assignment.done`,
`assignment.deadline_expired`. Эти события не должны менять бизнес-поведение
assignment-service; это факты после успешной команды.

**5F-2. `finance-service -> Kafka` — ✅ СДЕЛАНО**
Через outbox публиковать:
```text
payment_receipt.uploaded -> notification-service, report-service
payment.confirmed        -> notification-service, report-service
payment.rejected         -> notification-service, report-service
charge.created           -> report-service
balance.changed          -> report-service, notification-service при необходимости
```
Источник истины по балансу остаётся finance-service и append-only ledger;
`balance.changed` нужен read-models/notifications, а не для замены ledger.

**5F-3. Дополнительные lesson events**
`lesson.completed` уже внедрён. По мере появления потребителей добавить:
```text
lesson.scheduled
lesson.cancelled
lesson.rescheduled
```
Слушатели: notification-service и report-service. Finance остаётся consumer только
для `lesson.completed`.

Follow-up 5L/5F-3: `lesson.scheduled(origin=created)` эмитится из `CreateLesson`
в одной транзакции с insert занятия и используется notification-service для
уведомления ученика «Занятие назначено». `origin=reactivated` остаётся для
восстановления незавершённого отменённого занятия.

**5F-later. Не первоочередно**
`identity-service` events (`user.registered`, `student.created`,
`teacher_student_link.created`, `password.changed`) и `file-service` events
(`file.uploaded`, `file.deleted`) добавлять осторожно и только под конкретный
consumer. Для текущего домена важнее бизнес-события assignment/finance, чем сам
факт загрузки файла.

### Этап 5G — notification-service  ✅ СДЕЛАНО В ВЕТКЕ `feat/notification-service`
Первый полноценный Kafka consumer после расширения событий.

Минимальная версия:
```text
notification-service
  DB: notifications(id, user_id, type, title, body, payload, is_read, created_at)
  gRPC API: ListNotifications(user_id), MarkAsRead(notification_id)
  Kafka consumers:
    assignment.created       -> уведомить student
    submission.uploaded      -> уведомить teacher
    assignment.reviewed      -> уведомить student
    payment_receipt.uploaded -> уведомить teacher
    payment.confirmed        -> уведомить student
    payment.rejected         -> уведомить student
    lesson.completed         -> уведомить student
```
Gateway вызывает notification-service по gRPC, frontend показывает список
уведомлений и mark-as-read. Email/Telegram/push — позже, не в первой версии.

Реализация: отдельная `notification_db`, таблицы `notifications` и
`processed_events`, idempotency по `event_id`/`source_event_id`, gRPC health,
gateway endpoints `GET /notifications` и `POST /notifications/{notificationId}/read`.
Frontend показывает in-app уведомления в кабинетах teacher/student.

### Этап 5H — report-service / read-models  ⬜ ОСТАЁТСЯ
После notification-service. Цель — не собирать dashboard синхронными запросами к
4 сервисам, а строить read-models из событий.

Минимальная версия:
```text
report-service
  consumes lesson.*
  consumes assignment.*
  consumes finance.*
  builds:
    teacher_dashboard_summary
    student_dashboard_summary
    finance_summary
    assignment_stats
    lesson_stats
```
Gateway читает готовые dashboard по gRPC:
```text
api-gateway -> report-service: GetTeacherDashboard / GetStudentDashboard
```
Read-model не источник истины; при расхождении истина остаётся в доменных сервисах.

### Этап 5I — MinIO/S3 для file-service  ⬜ ОСТАЁТСЯ
Заменить local volume на object storage без изменения внешнего API:
```text
api-gateway -> file-service -> MinIO/S3
file-service -> file_db metadata
```
Нужно для lesson materials, assignment files, submission files, receipts и будущих
chat attachments. Метаданные остаются в `file_db`; сервисы по-прежнему хранят
только `file_id`.

### Этап 5J — chat-service  ⬜ ОСТАЁТСЯ
Делать после notification/report, потому что чат тянет realtime и статусы чтения.

Минимальная версия без realtime:
```text
chat-service
  gRPC API: CreateChat, SendMessage, ListMessages, MarkRead
  DB: dialogs, messages, message_attachments
  file-service: attachments через file_id
  Kafka: message.sent, message.read
```
Следующая итерация: WebSocket/SSE, Redis для online state/session routing,
unread counters. `message.sent` слушает notification-service.

### Этап 5K — production hardening  ⬜ ОСТАЁТСЯ
После расширения доменных возможностей:
```text
Caddy/Nginx reverse proxy
CI/CD и Docker image build
readiness checks отдельно от health
structured logs + request_id/trace_id
metrics
Kafka lag/retry/DLQ monitoring
backup/restore для Postgres и object storage
```

### Этап 5L — lesson lifecycle + finance corrections  ✅ СДЕЛАНО (5L.1–5L.9)
Полная спецификация и контракты: `docs/agent-lesson-lifecycle.md`. Согласовано с
человеком (2026-06-24). Расширяем жизненный цикл занятия и связь с финансами.

Охват:
- **reschedule** — перенос времени занятия (`scheduled`, новое время/слот; денег не касается);
- **reactivate** — восстановление отменённого (`cancelled → scheduled` или
  `cancelled → completed`, если занятие было завершено до отмены);
- **cancel completed** — отмена завершённого (`completed → cancelled`) с
  АВТОМАТИЧЕСКОЙ финансовой компенсацией;
- **manual correction** — ручная корректировка баланса ученика преподавателем.

Жёсткий инвариант: finance append-only — charge НЕ удаляем и НЕ создаём повторно
(`uq_charge_lesson` остаётся); откат/восстановление долга = пара `correction`
(-price/+price). Correction-пути (`lesson.cancelled`, `lesson.restored`)
идемпотентны по `event_id` через атомарный inbox `processed_events`, а не по
`lesson_id`, потому что у одного занятия может быть и отмена, и восстановление.
Принято product-решение: отмена оплаченного завершённого занятия может увести
баланс в минус (кредит ученику) — это допустимо.

Под-задачи (порядок и зависимости — см. design-doc §7):
```text
5L.0 контракты (координатор): proto lesson/finance + gateway/finance OpenAPI +
     event-contracts lesson.rescheduled/lesson.cancelled (JSON готовы)
5L.1 lesson: RescheduleLesson (+ слот, 409) + outbox lesson.rescheduled   ✅ СДЕЛАНО (feat/lesson-reschedule)
5L.2 lesson: ReactivateLesson (cancelled→scheduled, ребронь слота, идемпотентно)   ✅ СДЕЛАНО
5L.3 lesson: CancelLesson допускает completed→cancelled + outbox lesson.cancelled   ✅ СДЕЛАНО (feat/lesson-finance-corrections)
5L.4 finance: consumer lesson.cancelled → компенсирующая correction (idempotent по event_id inbox)   ✅ СДЕЛАНО
5L.5 finance: CreateCorrection (manual, ±amount + comment, check-access)   ✅ СДЕЛАНО
5L.6 gateway: роуты reschedule(✅)/reactivate(✅)/cancel(✅)/corrections(✅)   ✅ СДЕЛАНО
5L.7 notification-service: подписать новые события (rescheduled/cancelled/balance.changed)   ✅ СДЕЛАНО
5L.8 frontend: кнопки teacher (перенести/восстановить/отменить занятие, скорректировать баланс)   ✅ СДЕЛАНО
5L.9 tests + smoke: переходы, компенсация, идемпотентность, доступ   ✅ СДЕЛАНО (tests/test_corrections.py + smoke шаг 16)
5L follow-up: lesson.scheduled(origin=created) из CreateLesson; ReactivateLesson
для ранее completed занятия возвращает статус completed и эмитит lesson.restored;
finance consumer делает correction(+price) атомарно с processed_events; миграция
005 удаляет uq_correction_lesson; notification-service уведомляет lesson.created
и lesson.restored; tests покрывают recharge-cycle и replay без дублей.
```

5L.3-5L.6+5L.9 (feat/lesson-finance-corrections): CancelLesson разрешает
`completed→cancelled`, эмитит `lesson.cancelled` (previous_status, price/currency
только при completed) одной транзакцией; finance-консьюмер роутит по event_type и
на `lesson.cancelled(previous_status=completed)` добавляет `correction(-price)`
идемпотентно по `event_id` atomic inbox + `balance.changed`; `lesson.restored`
добавляет зеркальную `correction(+price)` тем же inbox-путём; gRPC `CreateCorrection`
(teacher + check-access, amount=0→422, comment обязателен), gateway-роут
`POST /students/{id}/corrections`. charge не удаляется (append-only).
**Грабли (важно):** топики Kafka создаются лениво (auto-create на первый publish).
finance-консьюмер с дефолтным `topic_metadata_refresh_interval` (5 мин) не
подхватывал только что созданный `tutorflow.lesson.cancelled` вовремя → компенсация
задерживалась. Фикс: `topic_metadata_refresh_interval: 3s` в finance kafka-consumer.
Контракты менялись (finance.proto + gateway/finance OpenAPI) → перед мержем
PR/подтверждение координатора.

5L.7+5L.8 (feat/lesson-finance-corrections): notification-service — кейсы
`lesson.rescheduled`/`lesson.scheduled(origin=reactivated)`/`lesson.cancelled`/
`balance.changed` (только `reason=correction.created`, чтобы не дублировать
charge/payment-уведомления) → уведомления ученику; +4 топика и `topic_metadata_refresh_interval: 3s`.
frontend (Teacher): у занятия кнопки Перенести (inline-форма)/Восстановить/Отменить
(вкл. completed); карточка финансов — журнал операций + форма ручной коррекции
(`POST /students/{id}/corrections`), обновление баланса/журнала; ошибки envelope
показываются. Все запросы — в gateway. Контракты G/H НЕ меняли (обычный PR).
5L.1 (feat/lesson-reschedule): RescheduleLesson — только teacher, только `scheduled`,
ownership+check-access, атомарная ребронь слота (409 если занят), идемпотентный no-op,
`lesson.rescheduled` в outbox одной транзакцией. Попутно фикс cast-бага
(`uuid = text`) в освобождении слота `CancelLesson`. Контракты менялись → перед
мержем PR/подтверждение координатора. NB: ветка от чистого `main` (без 5G).

Решённые edge-cases (design-doc §4): после компенсации `unique(lesson_id)` не даёт
повторного авто-charge → повторное начисление через ручную correction; reschedule/
reactivate занятия в прошлом — разрешаем. Зависимость: `lesson.scheduled` (5F-3)
нужен для reactivate; notification-service (5G) — потребитель новых событий.

### Что НЕ делать
file upload на gRPC не переводить (multipart на HTTP); access-check через Kafka —
нельзя (gRPC); gateway-вызовы Kafka-командами — нет; не делать все события сразу;
report-service до Kafka/outbox не делать; chat-service не делать раньше
notification/report; YDB/разделение identity на auth/user и реальные платежи —
только отдельными будущими решениями.

---

## Операционная закалка «из коробки»  ✅ СДЕЛАНО (после аудита)
Аудит подтвердил: связи и логика работают как задумано, e2e зелёный, блокеров нет.
Закрыты 3 эксплуатационные SHOULD-FIX:
- **авто-миграции**: one-shot `migrator` (ждёт postgres healthy, применяет
  `migrations/<svc>/*.sql` идемпотентно), сервисы `depends_on:
  migrator(service_completed_successfully)` — гонка устранена, `migrate.sh` остаётся валиден;
- **сборка без OOM**: `ARG BUILD_JOBS` во всех Dockerfile (дефолт 2) + штатная
  `COMPOSE_PARALLEL_LIMIT=1 docker compose build`;
- **TS6310**: `tsconfig.node.json` — `noEmit` → `emitDeclarationOnly`+`outDir`; `npm run build` зелёный.
Чистый bring-up одной последовательностью: build → `up -d` (авто-миграция) → `SMOKE OK`.
Доки синхронизированы (AGENTS §Стек, PLAN §1/§17).

Косметика: мёртвые `*_SERVICE_URL` и устаревший комментарий kafka-блока — ✅ убраны
(enum `UpstreamService` сведён к `kFile`). Остаётся: secdist-адрес брокера (не критично);
UI материалов урока (3.6.3, backend готов) — в работе.

---

## Backlog (не в критическом пути MVP)
- **file-service: хранить опциональный `resource_id`** в метаданных файла для
  аудита и кросс-ресурсного поиска. Сейчас связь уже держится на стороне
  потребителей (`assignment_files.file_id`, `submission_files.file_id`,
  `payment_receipts.file_id`), поэтому не блокирует MVP.
  Приоритет: low до фронта, medium до chat/report.

---

## Правила назначения задач
- В один момент активен **один** агент с **одной** задачей (PLAN §13).
- Человек называет агенту задачу по номеру (напр. «Этап 2.5.1»).
- Зависимости: UI gaps закрывать до новых сервисов; 5F-0 перед 5F-1/5F-2;
  notification-service после появления assignment/finance events; report-service
  после стабильных event contracts; chat-service после notification/report.
- Менять контракты (api-contracts) и public-сигнатуры `libs/common` — только через
  координатора (Claude) с подтверждением человека.
