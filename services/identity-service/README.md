# identity-service

`identity-service` — источник истины по пользователям, ролям, профилям и связи
«преподаватель ↔ ученик». Здесь же находятся пароли и выпуск JWT.

Объединение auth и user-профилей осознанно: эти данные сильно связаны, а
разделение добавило бы сетевой вызов на горячем пути проверки доступа. Решение
зафиксировано в [ADR 0001](../../docs/adr/0001-identity-combines-auth-and-user.md).

[Вернуться к общей архитектуре](../../README.md)

## Ответственность

- регистрация преподавателя или ученика;
- вход по email и паролю;
- выпуск и проверка JWT;
- смена пароля;
- получение пользователя;
- создание ученика преподавателем;
- список и карточка учеников преподавателя;
- каноническая проверка `CheckTeacherStudentAccess`;
- хранение почасовой ставки в связи teacher-student.

`identity-service` не хранит занятия, задания, деньги или файлы и не принимает
решения за соответствующие доменные сервисы.

## Почему access-check находится здесь

Другие сервисы знают только `teacher_id` и `student_id`. Чтобы проверить связь,
они вызывают один RPC:

```text
lesson / assignment / finance / file / chat
    └── gRPC CheckTeacherStudentAccess
            └── identity_db.teacher_student_links
```

Сервис возвращает `allowed`, статус связи и `hourly_rate`. Это не даёт другим
сервисам права читать `identity_db` напрямую.

## gRPC API

Контракт: [`identity.proto`](../../libs/proto/tutorflow/identity.proto).

| RPC | Назначение |
|---|---|
| `Register` | создать пользователя и вернуть token |
| `Login` | проверить пароль и вернуть token |
| `ValidateToken` | разобрать и проверить JWT |
| `ChangePassword` | проверить текущий пароль и сохранить новый hash |
| `GetUser` | получить публичные данные пользователя |
| `GetStudent` | получить связь/карточку ученика |
| `ListStudents` | список учеников преподавателя |
| `CreateStudent` | создать student и связь с teacher |
| `CheckTeacherStudentAccess` | единая межсервисная проверка доступа |

Публичный клиент вызывает эти методы через
[`api-gateway`](../api-gateway/README.md), а не напрямую.

## Пароли и JWT

- пароль хранится как `PBKDF2-HMAC-SHA256`;
- используется случайная 16-байтовая salt;
- текущая реализация выполняет 100 000 итераций и получает 32-байтовый hash;
- сравнение hash выполняется через constant-time `CRYPTO_memcmp`;
- JWT содержит `sub`, список ролей, `iat` и `exp`;
- подпись и проверка JWT вынесены в `libs/common`;
- TTL задаётся `JWT_TTL_SECONDS`, значение по умолчанию — 86 400 секунд.

Это достаточная модель для учебного MVP. Refresh token, revoke-list, OAuth,
MFA и отдельное управление сессиями не реализованы.

## Данные

Сервис владеет `identity_db`.

| Таблица | Содержание |
|---|---|
| `users` | email, password hash, роль, время создания |
| `teacher_profiles` | display name и timezone преподавателя |
| `student_profiles` | display name ученика |
| `teacher_student_links` | связь, предмет, цель, ставка и статус |

Email уникален, поэтому параллельная повторная регистрация не создаёт второго
пользователя. Другие сервисы хранят UUID пользователей, но не копируют профили.

## Внутренняя структура

```text
src/main.cpp
  ├── IdentityGrpcService       transport и protobuf mapping
  ├── IdentityService           validation, passwords, JWT, domain rules
  ├── IdentityRepository        SQL только к identity_db
  └── HealthHandler/ReadyHandler
```

Обычный путь запроса:

```text
gateway handler
  → gateway GrpcIdentityClient
  → IdentityGrpcService
  → IdentityService
  → IdentityRepository
  → identity_db
```

## События

Сейчас identity не публикует и не потребляет Kafka events. События вроде
`user.registered` или `student.created` намеренно отложены до появления
реального потребителя. Это не мешает синхронным auth/access-check сценариям.

## Конфигурация и runtime

| Настройка | Назначение |
|---|---|
| `IDENTITY_DATABASE_URL` | DSN собственной БД |
| `JWT_SECRET` | подпись JWT; совпадает с gateway |
| `JWT_TTL_SECONDS` | срок жизни access token |

Сервис слушает gRPC `9081`; HTTP `8081` используется для эксплуатационных
endpoint-ов внутри сети. `/ready` выполняет проверку собственной PostgreSQL БД,
а `/health` остаётся лёгким liveness probe.

## Как проверить

```bash
docker compose build identity-service
docker compose up -d identity-service api-gateway
curl http://localhost:8080/health
python3 -m pytest tests/test_auth.py tests/test_access.py -v
```

Основные источники:

- [protobuf-контракт](../../libs/proto/tutorflow/identity.proto);
- [domain service](src/domain/identity_service.cpp);
- [repository](src/repositories/identity_repository.cpp);
- [миграции](../../migrations/identity/);
- [ADR о границе сервиса](../../docs/adr/0001-identity-combines-auth-and-user.md).
