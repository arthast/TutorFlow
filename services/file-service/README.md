# file-service

`file-service` — единственное место, через которое TutorFlow сохраняет и отдаёт
файлы. В базе находятся метаданные, а байты хранятся на локальном диске или в
S3-compatible storage.

Остальные сервисы сохраняют только `file_id`. Это отделяет бизнес-сущности от
способа хранения blob-данных.

[Вернуться к общей архитектуре](../../README.md)

## Что поддерживается

- HTTP multipart upload;
- получение metadata;
- скачивание содержимого;
- ограничения по размеру;
- проверка owner и связи teacher-student;
- purpose: `assignment_attachment`, `submission_file`, `payment_receipt`,
  `lesson_material`, `chat_message`;
- storage backend `local` или `s3` без изменения внешнего API;
- автоматическое создание S3 bucket при старте, если он отсутствует.

Удаление публичным API и версионирование файлов не реализованы.

## Почему здесь HTTP, а не gRPC

Большие multipart body уже естественно представлены в HTTP. Gateway передаёт
файл в `file-service` как поток внешнего формата, а metadata остальных доменов
остаётся в typed gRPC API.

```text
Browser
  → POST /files (multipart)
  → api-gateway: auth + trusted X-User headers
  → POST /internal/files (multipart)
  → file-service
      ├── object bytes → local volume или MinIO
      └── metadata     → file_db.files
  ← file_id
```

## Внутренний HTTP API

| Метод и путь | Назначение |
|---|---|
| `POST /internal/files` | загрузить файл |
| `GET /internal/files/{fileId}` | получить metadata |
| `GET /internal/files/{fileId}/download` | скачать содержимое |

Эти endpoint-ы предназначены для gateway и не публикуются клиенту напрямую.
Публичные schema находятся в
[`gateway.openapi.yaml`](../../docs/api-contracts/gateway.openapi.yaml), а
внутренние — в [`file.openapi.yaml`](../../docs/api-contracts/file.openapi.yaml).

## Доступ к файлам

В `files.owner_user_id` записывается загрузивший пользователь. Скачать файл
может:

- сам owner;
- связанный с owner преподаватель или ученик, если связь подтверждена через
  `identity-service.CheckTeacherStudentAccess`.

Такой симметричный доступ позволяет ученику скачать материал преподавателя, а
преподавателю — решение, чек или chat attachment ученика. File-service не
подключается к `identity_db`.

## Storage abstraction

Домен работает с интерфейсом `IFileStorage`:

```cpp
Put(storage_key, bytes, content_type)
Get(storage_key)
Delete(storage_key)
CheckReady()
```

Реализации:

- `LocalFileStorage` — файл в `FILE_STORAGE_DIR`;
- `S3FileStorage` — HTTP-запросы к MinIO/S3 с ручной AWS Signature Version 4.

AWS SDK не добавлен: используемый поднабор S3 небольшой, поэтому реализация
опирается на userver HTTP client. Компромисс — меньше зависимостей, но SigV4 и
совместимость S3 приходится поддерживать в проекте самостоятельно.

## Атомарность upload

1. Проверяются purpose, имя, MIME type и размер.
2. Генерируется случайный `storage_key`.
3. Байты записываются в выбранный storage.
4. Metadata вставляется в PostgreSQL.
5. Если запись metadata завершилась ошибкой, сервис пытается удалить уже
   сохранённый object.

Это компенсирующая очистка, а не распределённая транзакция между PostgreSQL и
S3. При аварийном завершении между шагами теоретически возможен orphan object;
фоновый garbage collector пока не реализован.

## Данные

`file_db.files` содержит:

```text
id, owner_user_id, purpose, original_name, content_type,
size_bytes, storage_key, created_at
```

`storage_key` уникален. В таблице нет самих байтов и нет внешних ключей в базы
assignment, lesson, finance или chat.

## Внутренняя структура

```text
src/main.cpp
  ├── handlers/file_handlers.*      HTTP parsing и response
  ├── domain/file_service.*         validation и access rules
  ├── repositories/file_repository.* metadata SQL
  ├── storages/file_storage.*       local/S3 adapters
  └── handlers/ready_handler.*
```

## Конфигурация

| Переменная | Назначение |
|---|---|
| `FILE_DATABASE_URL` | DSN `file_db` |
| `FILE_STORAGE_BACKEND` | `local` или `s3` |
| `FILE_STORAGE_DIR` | путь локального backend |
| `FILE_MAX_SIZE_BYTES` | максимальный размер, по умолчанию 10 MiB |
| `FILE_S3_ENDPOINT` | адрес MinIO/S3 |
| `FILE_S3_BUCKET` | bucket, обычно `tutorflow-files` |
| `FILE_S3_REGION` | регион для SigV4 |
| `MINIO_ROOT_USER/PASSWORD` | dev/demo credentials через secdist |

Gateway и file handler допускают HTTP body до 12 MiB, оставляя запас над
дефолтным доменным лимитом 10 MiB.

## Health и readiness

- `/health` не обращается к внешним ресурсам;
- `/ready` проверяет `file_db` и активный storage backend;
- для S3 выполняется проверка доступности bucket;
- `/metrics` доступен только на monitor listener `18085` внутри сети.

## Как проверить

```bash
docker compose build file-service api-gateway
docker compose up -d minio file-service api-gateway
curl http://localhost:8080/health
python3 -m pytest tests/test_access.py tests/test_finance.py tests/test_chat.py -v
```

Основные источники:

- [file handlers](src/handlers/file_handlers.cpp);
- [domain service](src/domain/file_service.cpp);
- [storage implementations](src/storages/file_storage.cpp);
- [миграции](../../migrations/file/);
- [внутренний OpenAPI](../../docs/api-contracts/file.openapi.yaml).
