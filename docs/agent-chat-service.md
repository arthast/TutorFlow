# Этап 5J — chat-service (design + контракты)

> Координаторский спек под выдачу агенту. Источник правды по эндпоинтам — `docs/api-
> contracts/*` и `libs/proto/*`; event-контракты — `docs/event-contracts/message.*`.
> Здесь — ЧТО и КАК строим. Дата: 2026-06-26.

## 1. Цель и охват

Личная переписка teacher ↔ student внутри платформы: диалоги, сообщения, вложения,
отметки прочтения. **Без realtime** в v1 — обновление через polling. Вложения — через
существующий file-service (`file_id`). Уведомление о новом сообщении — через
notification-service (consumer `message.sent`).

**Не делаем в v1 (5J-later / 5K):** WebSocket/SSE, Redis для online-состояния и
unread-кэша, групповые чаты, реакции, редактирование/удаление сообщений, typing-индикаторы.

## 2. Принципы / правила проекта

- Новый внутренний сервис: только gRPC-сервер + (как producer) Kafka outbox. Своя БД
  `chat_db`. Наружу — только через gateway (REST). Чужие БД не читает.
- Диалог строго **между связанными teacher и student** — проверка пары через identity
  `CheckTeacherStudentAccess` (как везде). Слать/читать может только участник диалога.
- Файлы — только через file-service (`purpose = chat_message`); в chat хранится `file_id`.
- События — через transactional outbox (как lesson/finance). chat в v1 только ПРОИЗВОДИТ
  события (`message.sent`, `message.read`), consumer-ов не держит → inbox не нужен.

## 3. Модель данных (`chat_db`)

```sql
dialogs(
  id UUID PK, teacher_id UUID, student_id UUID,
  created_at, last_message_at,
  UNIQUE (teacher_id, student_id))          -- один диалог на пару

messages(
  id UUID PK, dialog_id UUID FK, sender_id UUID,
  text TEXT, created_at)

message_attachments(
  id UUID PK, message_id UUID FK, file_id UUID, created_at)

-- read-маркер на участника: указатель «прочитано до»
read_markers(
  dialog_id UUID, user_id UUID,
  last_read_message_id UUID, last_read_at TIMESTAMPTZ, updated_at,
  PRIMARY KEY (dialog_id, user_id))

outbox_events(... как в других сервисах ...)
```
`unread_count` для участника = число сообщений в диалоге позже его `last_read_message_id`
и **не им** отправленных (считаем запросом, не храним счётчик).

## 4. gRPC API (`libs/proto/tutorflow/chat.proto`)

```proto
service ChatService {
  rpc CreateDialog(CreateDialogRequest) returns (Dialog);   // find-or-create по паре, идемпотентно
  rpc ListDialogs(ListDialogsRequest) returns (ListDialogsResponse); // диалоги звонящего + last_message + unread_count
  rpc SendMessage(SendMessageRequest) returns (Message);    // text + file_ids[]; только участник
  rpc ListMessages(ListMessagesRequest) returns (ListMessagesResponse); // пагинация
  rpc MarkRead(MarkReadRequest) returns (ReadMarker);       // сдвинуть указатель до up_to_message_id
}
```
- `CreateDialog`: caller + `other_user_id`; пара teacher↔student определяется по ролям;
  `CheckTeacherStudentAccess` обязателен; идемпотентно (UNIQUE по паре → find-or-create).
- `SendMessage(dialog_id, text, file_ids[])`: только участник диалога; пишет message +
  attachments + `message.sent` в outbox ОДНОЙ транзакцией; `recipient_id` = второй участник.
- `MarkRead(dialog_id, up_to_message_id)`: указатель только вперёд (max); пишет
  `message.read` в outbox; идемпотентно (повтор не двигает назад, дубль события безвреден).

## 5. События

Новые контракты (готовы): `docs/event-contracts/message.sent.v1.json`,
`message.read.v1.json`. Через outbox, ключ `dialog_id`.
- `message.sent` → **notification-service**: уведомить `recipient_id` («Новое сообщение»,
  опционально preview). report-service позже.
- `message.read` → в v1 потребителей нет (задел под unread read-models). Полный текст
  сообщения в события НЕ кладём — только опциональный короткий `preview` в `message.sent`.

## 6. Зависимости на другие сервисы

- **file-service:** добавить `chat_message` в enum `purpose` (миграция file-service;
  PLAN §8.5 это значение зарезервировал «позже»). Доступ на скачивание вложений уже
  покрыт симметричным teacher↔student доступом file-service — отдельных правок не нужно.
- **notification-service:** новый кейс `message.sent` → уведомление `recipient_id`
  (идемпотентность/скоуп — существующий механизм `processed_events` + unique).
- **identity:** `CheckTeacherStudentAccess` (уже есть).
Все три — согласовать как кросс-сервисные правки с координатором.

## 7. Gateway (внешние REST-эндпоинты)

```text
POST /chats                     body { other_user_id }            -> CreateDialog (find-or-create)
GET  /chats                                                       -> ListDialogs (+ unread_count)
GET  /chats/{dialogId}/messages ?before=&limit=                   -> ListMessages
POST /chats/{dialogId}/messages body { text, file_ids? }          -> SendMessage
POST /chats/{dialogId}/read     body { up_to_message_id }          -> MarkRead
```
Вложения: клиент сперва грузит файл `POST /files` (`purpose=chat_message`) → `file_id` →
передаёт в `file_ids`. Gateway тонкий (auth + routing + mapping), без бизнес-логики.

## 8. Frontend (v1, polling)

Простой teacher↔student чат: список диалогов с unread, окно сообщений (periodic refetch),
форма отправки текста + вложение, mark-read при открытии диалога. Realtime не делаем.

## 9. Definition of Done

1. `docker compose build chat-service` — OK; миграции `chat_db` (+ file `purpose=chat_message`) на чистую БД.
2. Полный путь через gateway: создать диалог → отправить сообщение (текст и с вложением) →
   собеседник видит сообщение и получает уведомление (`message.sent` → notification) →
   mark-read уменьшает unread.
3. Доступ: не-участник не может слать/читать диалог (403); пара не связана (check-access) → 403.
4. Вложение скачивается обеими сторонами (симметричный доступ file-service).
5. Идемпотентность: `CreateDialog` по паре не плодит диалоги; `MarkRead` только вперёд.
6. Outbox: `message.sent`/`message.read` публикуются (status published).
7. `scripts/smoke_mvp.py` → SMOKE OK; `pytest tests` зелёный (+ chat-кейсы); фронт build OK.
8. chat наружу не публикуется (только gateway); chat ничьи БД не читает.

## 10. Что НЕ делаем в 5J

Realtime (WebSocket/SSE), Redis, групповые чаты, редактирование/удаление, typing, реакции,
полнотекстовый поиск. message.read-потребителей в v1 нет (только задел). Полный текст в
события не кладём.
