# Этап 5J-later — realtime-service (WebSocket + Redis)

## Цель

`realtime-service` — отдельный публичный WebSocket push-канал для уже
произошедших событий чата и уведомлений. Домен не меняется: отправка сообщений
остаётся REST -> api-gateway -> chat-service -> `message.sent` (Kafka).
Realtime-service не создаёт сообщения, уведомления, read-маркеры или платежи.

## Публичный endpoint

```text
ws://localhost:8089/ws?token=<jwt>
```

JWT передаётся в query-параметре `token`, потому что браузерный WebSocket API не
позволяет штатно задать `Authorization`. Токен валидируется локально тем же
`JWT_SECRET`, что использует gateway. Без токена, с битым токеном или истёкшим
токеном handshake отклоняется.

## Server -> client

Все сообщения JSON:

```json
{ "type": "chat.message", "payload": { "...": "..." } }
```

Типы:

- `chat.message`: новое сообщение получателю. Payload строится из
  `message.sent`: `dialog_id`, `message_id`, `sender_id`, `recipient_id`,
  `teacher_id`, `student_id`, `has_attachments`, `preview`, `sent_at`,
  `unread_count`.
- `chat.read`: собеседник прочитал сообщения. Payload строится из
  `message.read`: `dialog_id`, `reader_id`, `up_to_message_id`, `read_at`.
- `presence`: online/offline статус пользователя: `user_id`, `online`.
- `notification`: новое in-app уведомление. Payload строится из
  `notification.created`: `user_id`, `notification_id`, `type`, `title`,
  `body`, `created_at`.
- `pong`: ответ на client `ping`.

## Client -> server

- `ping`: heartbeat; realtime обновляет presence TTL и отвечает `pong`.
- `typing`: опционально, best-effort; доменное состояние не меняет.

## Redis

- `rt:presence:{user_id}` -> `"1"`, TTL. Обновляется на connect и heartbeat.
- `rt:user:{user_id}` — pub/sub канал доставки в инстанс, где есть соединение
  пользователя.
- `rt:unread:{user_id}:{dialog_id}` — realtime-счётчик бейджа; source of truth
  остаётся chat-service (`ListDialogs`).
- `rt:dialog:{dialog_id}:participants` — кэш участников, заполненный из
  `message.sent`, чтобы доставлять `message.read` второму участнику без чтения
  `chat_db`.
- `rt:user_peers:{user_id}` — кэш известных собеседников для presence fan-out.

## Kafka

Realtime-service состоит в общей consumer group `realtime-domain-events`, чтобы
каждое доменное событие обрабатывал один инстанс. На обработке он публикует
сообщение в Redis `rt:user:{recipient}`; все инстансы слушают pub/sub и пушат
только локально подключённым пользователям.

События:

- `message.sent` -> `chat.message` получателю + `INCR rt:unread:{recipient}:{dialog_id}`.
- `message.read` -> `chat.read` второму участнику + `DEL rt:unread:{reader}:{dialog_id}`.
- `notification.created` -> `notification` адресату.

## Не входит

Rate limiting сообщений, edit/delete, sticky sessions, WebSocket realtime для
команд, отдельный presence feed и production hardening остаются на 5K/later.
