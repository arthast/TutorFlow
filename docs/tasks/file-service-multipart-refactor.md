# Задача: file-service — переход на встроенный multipart-парсер userver + фикс max_request_size

Дата постановки: 2026-07-07. Контекст собран координатором (Claude) по итогам ревью
`file_handlers.cpp` и проверки по официальной документации userver.

## Цель

Две связанные правки, одна ветка `feat/file-multipart-userver`:

1. Заменить ручной multipart-парсер в file-service на встроенный API userver
   (`HasFormDataArg` / `GetFormDataArg`).
2. Исправить latent bug: дефолтный `max_request_size` userver = 1 MiB на handler,
   при этом `FILE_MAX_SIZE_BYTES=10485760` (10 MB). Сейчас загрузка файла >1MB
   получает 413 от фреймворка ещё до доменной проверки размера — и на gateway,
   и на file-service.

## Что менять

### 1. `services/file-service/src/handlers/file_handlers.cpp`

Удалить полностью (сейчас ~строки 31–154):

- `struct MultipartField`
- `ExtractBoundary(...)`
- `ExtractCDParam(...)`
- `ParseMultipart(...)`

Переписать `ParseUploadRequest` на встроенный API. Эталон:

```cpp
ParsedUpload ParseUploadRequest(const http::HttpRequest& req) {
    const auto& ct_header = req.GetHeader("Content-Type");
    if (ct_header.find("multipart/form-data") == std::string::npos) {
        throw ServiceError(http::HttpStatus::kUnsupportedMediaType,
                           "unsupported_media_type",
                           "Content-Type must be multipart/form-data");
    }
    if (!req.HasFormDataArg("purpose")) {
        throw ServiceError::Validation("missing required field: purpose");
    }
    if (!req.HasFormDataArg("file")) {
        throw ServiceError::Validation("missing required field: file");
    }
    const auto& purpose_arg = req.GetFormDataArg("purpose");
    const auto& file_arg = req.GetFormDataArg("file");

    ParsedUpload result;
    result.purpose = std::string{purpose_arg.value};
    result.original_name =
        (file_arg.filename && !file_arg.filename->empty()) ? *file_arg.filename
                                                           : "upload";
    result.content_type = file_arg.content_type
                              ? std::string{*file_arg.content_type}
                              : "application/octet-stream";
    result.data = std::string{file_arg.value};

    static constexpr std::string_view kValidPurposes[] = {
        "assignment_attachment", "submission_file", "payment_receipt",
        "lesson_material", "chat_message"};
    bool valid_purpose = false;
    for (auto p : kValidPurposes) {
        if (result.purpose == p) { valid_purpose = true; break; }
    }
    if (!valid_purpose) {
        throw ServiceError::Validation("invalid purpose value");
    }
    return result;
}
```

Справка по `FormDataArg` (проверено по докам userver):

```cpp
struct FormDataArg {
    std::string_view value;                        // байты поля/файла, указывает в тело запроса
    std::string_view content_disposition;
    std::optional<std::string> filename;           // именно optional<string>, не string_view
    std::optional<std::string> default_charset;
    std::optional<std::string_view> content_type;  // Content-Type части multipart
};
```

Парсинг multipart в userver автоматический (по Content-Type запроса), никакой
конфигурации не нужно. `value` — string_view в тело запроса: внутри хендлера
валиден, копируем в `ParsedUpload` (как в эталоне) — этого достаточно.
При необходимости добавить `#include <userver/server/http/form_data_arg.hpp>`;
почистить ставшие ненужными include (`<vector>`, возможно `<optional>`).

### 2. `services/file-service/configs/static_config.yaml`

У `file-upload-handler` добавить:

```yaml
        file-upload-handler:
            path: /internal/files
            method: POST
            task_processor: main-task-processor
            max_request_size: 12582912   # 12 MiB: 10 MiB файл + multipart-оверхед
```

### 3. `services/api-gateway/configs/static_config.yaml`

То же самое у `gateway-files-handler` (`max_request_size: 12582912`) — gateway
проксирует multipart сырым телом и режет запрос тем же дефолтом в 1 MiB.

## Что НЕ менять

- Контракт API (пути, envelope ошибок, формат ответа) — не меняется.
- `FileService` / `FileRepository` / `IFileStorage` / gateway proxy-логику — не трогать.
- Доменная проверка 10 MB в `FileService::Upload` остаётся (max_request_size — это
  только транспортный лимит).
- Не добавлять зависимостей, не рефакторить соседние файлы.

## Сохранить поведение (acceptance)

1. Не-multipart Content-Type → 415 `unsupported_media_type` в envelope.
2. Нет поля `purpose` / нет поля `file` → 400 validation в envelope.
3. `purpose` вне whitelist (5 значений выше) → 400.
4. Файл без filename → `original_name = "upload"`; без Content-Type части →
   `application/octet-stream`.
5. Успех → 201 Created + JSON метаданных (как сейчас).
6. Файл >10 MB → 413 `too_large` из доменной проверки (а не обрыв на транспорте).
7. НОВОЕ: файл 2–5 MB успешно загружается через gateway (сейчас это сломано).

## Как проверить

```bash
docker compose build file-service api-gateway
docker compose up -d
# токен студента получить как обычно (login через gateway)

# 3 MB файл — раньше падал с 413 до доменной логики
dd if=/dev/urandom of=/tmp/big.bin bs=1M count=3
curl -i -X POST http://localhost:8080/files \
  -H "Authorization: Bearer $TOKEN" \
  -F "purpose=payment_receipt" -F "file=@/tmp/big.bin" \
  # ожидание: 201

# 11 MB — должен ответить доменный 413 too_large в envelope
dd if=/dev/urandom of=/tmp/huge.bin bs=1M count=11
curl -i -X POST http://localhost:8080/files \
  -H "Authorization: Bearer $TOKEN" \
  -F "purpose=payment_receipt" -F "file=@/tmp/huge.bin"

# ошибки валидации
curl -i -X POST http://localhost:8080/files -H "Authorization: Bearer $TOKEN" \
  -F "file=@/tmp/big.bin"                      # без purpose -> 400
curl -i -X POST http://localhost:8080/files -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" -d '{}'  # -> 415

# скачивание обратно
curl -i http://localhost:8080/files/<fileId>/download -H "Authorization: Bearer $TOKEN"
```

## Definition of Done (по AGENTS.md)

1. Оба сервиса собираются, `/health` отвечает.
2. Все пункты acceptance проходят через gateway.
3. Ошибки в едином envelope.
4. Перечислить изменённые файлы; отметить, что контракт не менялся.
5. Ветка `feat/file-multipart-userver` от актуального `main`
   (`git pull --rebase origin main` перед стартом).

## Ссылки

- Туториал userver по multipart: https://userver.tech/df/d0f/md_en_2userver_2tutorial_2multipart__service.html
- FormDataArg: https://userver.tech/db/d3b/form__data__arg_8hpp_source.html
- max_request_size (HandlerBase, дефолт 1 MiB): https://userver.tech/docs/v2.0/d1/ddc/classserver_1_1handlers_1_1HandlerBase.html
