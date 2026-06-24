# TutorFlow — Roadmap (общий, для любого агента)

> Очередь работ. Привязки «сервис → агент» нет: исполнителя на каждую задачу
> выбирает человек, включая нужного агента (Claude или Codex) поочерёдно.
> Координатор — Claude (контракты, PLAN, ревью, интеграция). Источники правды:
> `docs/PLAN.md`, `docs/api-contracts/*.openapi.yaml`, общие правила — `AGENTS.md`.
>
> Стадия проекта: **не «архитектура на бумаге», а стабилизация MVP.** Новые
> сервисы сейчас НЕ добавляем. Цель этапа — довести 6 готовых сервисов до
> демонстрируемого продукта: сквозной сценарий + тесты + простой UI + деплой.

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

> Гейт: gRPC-трек (5A–5C) сделан; Kafka-трек (5D–5E) сделан; дальше 5F (события) и
> 5G (сервисы-консьюмеры). Все proto/event-контракты — через координатора (Claude).

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

### Этап 5F — остальные события  ⬜ ОСТАЁТСЯ
По надобности (на старте слушателей может не быть): `assignment.created`,
`submission.uploaded`, `assignment.reviewed`, `payment_receipt.uploaded`,
`payment.confirmed`, `payment.rejected`, `charge.created`, `balance.changed`.
Тот же outbox-паттерн, что в 5E (вынести общий outbox-код в libs).

### Этап 5G — новые сервисы (consumers)  ⬜ ОСТАЁТСЯ
```text
1 notification-service  — слушает события, пишет notification records (email/Telegram позже)
2 report-service        — read-model consumer: агрегаты из событий (не раньше Kafka)
3 chat-service          — и только здесь: Redis + WebSocket/SSE, read-status, online-state
```
Позже: identity → `auth-service` + `user-service`; MinIO/S3 вместо локального storage.

### Что НЕ делать
file upload на gRPC не переводить (multipart на HTTP); access-check через Kafka —
нельзя (gRPC); gateway-вызовы Kafka-командами — нет; не делать все события сразу;
report-service до Kafka/outbox не делать.

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
- Зависимости: Этап 2.5 — после 1.2/1.6; Этап 3 — после 2.5 (и 1.4, готово).
- Менять контракты (api-contracts) и public-сигнатуры `libs/common` — только через
  координатора (Claude) с подтверждением человека.
