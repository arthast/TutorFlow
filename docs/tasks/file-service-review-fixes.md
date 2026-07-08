# Задача: file-service — фиксы по внешнему ревью (этап 3)

Дата постановки: 2026-07-07. Координатор (Claude) разобрал внешнее ревью из 15
пунктов; ниже — только принятые. Отклонены координатором (НЕ делать):
ACL по purpose (учитель обязан видеть payment_receipt ученика — он подтверждает
оплату; entity-links в file-service — размывание границ), синхронизация purpose
с CHECK (уже сделана миграциями 002/003), лимит body (уже сделан max_request_size),
каст size_t/int64_t (косметика), CHECK длины в БД (хендлера достаточно).

## Ветка

Продолжаем `feat/file-multipart-userver`. ВАЖНО: сначала закоммитить уже готовые
этапы 1–2 (6 файлов: file_handlers.cpp, file_service.hpp/cpp, оба
static_config.yaml, file.openapi.yaml) отдельным коммитом, потом этот этап —
вторым коммитом. docs/tasks/ не коммитить.

## Фикс 1. std::move файла в UploadHandler (лишняя копия 10MB)

`services/file-service/src/handlers/file_handlers.cpp`, `UploadHandler`:
сейчас `const auto upload = ParseUploadRequest(request);` и `upload.data`
передаётся lvalue в `Upload(std::string data)` — полная копия файла.

```cpp
auto upload = ParseUploadRequest(request);   // НЕ const
return JsonResponse(
    request,
    ToJson(service_.Upload(auth.user_id, upload.purpose,
                           upload.original_name, upload.content_type,
                           std::move(upload.data))),
    http::HttpStatus::kCreated);
```

## Фикс 2. Компенсационное удаление при падении SaveFileMeta

`file_service.cpp`, `Upload`: если `storage_.Put` успешен, а insert метаданных
упал — блоб остаётся сиротой. Обернуть:

```cpp
storage_.Put(storage_key, std::move(data), content_type);
try {
    return repository_.SaveFileMeta(
        owner_user_id, purpose, original_name, content_type,
        data_size, storage_key);
} catch (...) {
    try {
        storage_.Delete(storage_key);
    } catch (const std::exception& e) {
        LOG_ERROR() << "failed to cleanup orphan storage object "
                    << storage_key << ": " << e;
    }
    throw;
}
```

`#include <userver/logging/log.hpp>`.

## Фикс 3. Атомарная запись в LocalFileStorage::Put

`file_storage.cpp`: писать во временный файл, проверять поток, затем rename
(rename в пределах одной ФС атомарен; storage_key — UUID, коллизий tmp нет):

```cpp
userver::engine::AsyncNoTracing(fs_tp_, [path, d = std::move(bytes)]() {
    const std::filesystem::path final_path{path};
    const std::string tmp_path = path + ".tmp";
    std::filesystem::create_directories(final_path.parent_path());
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot open file for writing: " + tmp_path);
        }
        out.write(d.data(), static_cast<std::streamsize>(d.size()));
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(tmp_path, ec);
            throw std::runtime_error("failed to write file: " + tmp_path);
        }
    }
    std::filesystem::rename(tmp_path, final_path);
}).Get();
```

## Фикс 4. Санитизация content_type и лимит original_name при upload

В `file_handlers.cpp` (anonymous namespace):

```cpp
bool IsSafeHeaderValue(std::string_view value) {
    if (value.empty() || value.size() > 255) return false;
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

// Обрезка по границе UTF-8-символа (не рвать многобайтовый символ).
void TruncateUtf8(std::string& s, std::size_t max_bytes) {
    if (s.size() <= max_bytes) return;
    s.resize(max_bytes);
    while (!s.empty() &&
           (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
        s.pop_back();
    }
}
```

В конце `ParseUploadRequest` перед `return`:

```cpp
if (!IsSafeHeaderValue(result.content_type)) {
    result.content_type = "application/octet-stream";
}
TruncateUtf8(result.original_name, 255);
if (result.original_name.empty()) result.original_name = "upload";
```

Миграцию с CHECK на длину НЕ добавлять (решение координатора).

## Фикс 5. Content-Disposition с filename* (RFC 5987) для кириллицы

Имена файлов у пользователей русские — голый `filename="<utf-8>"` нестабилен
между клиентами. В `DownloadHandler` отдавать оба параметра:

```cpp
// ASCII-fallback: не-ASCII → '_', плюс уже существующая санитизация.
std::string AsciiFallbackFilename(const std::string& sanitized) {
    std::string out;
    out.reserve(sanitized.size());
    for (unsigned char c : sanitized) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '_');
    }
    if (out.empty()) out = "upload";
    return out;
}

// Percent-encoding всех байт кроме unreserved (RFC 3986/5987).
std::string PercentEncodeUtf8(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}
```

И заголовок:

```cpp
const auto safe_name = SanitizeDispositionFilename(meta.original_name);
request.GetHttpResponse().SetHeader(
    std::string{"Content-Disposition"},
    "attachment; filename=\"" + AsciiFallbackFilename(safe_name) +
        "\"; filename*=UTF-8''" + PercentEncodeUtf8(safe_name));
```

`SanitizeDispositionFilename` из этапа 2 остаётся как есть. Дублирование
percent-encode с storage-кодом допустимо — в libs/common НЕ выносить (правило
«не трогать общие либы без согласования»).

## Фикс 6. Потеря объекта хранилища → единый 500 (оба бэкенда)

Метаданные есть, байтов нет — это нарушение консистентности, а не "not found".
В `file_storage.cpp`, `S3FileStorage::Get`, ветку 404 заменить:

```cpp
if (status == 404) {
    throw tutorflow::common::ServiceError::Internal(
        "storage object missing for existing metadata");
}
```

Local-бэкенд уже кидает `std::runtime_error` → 500, поведение совпадёт.
В openapi у download 404 остаётся (нет метаданных — честный 404).

## Фикс 7. FindById на master

`file_repository.cpp`: в `FindById` заменить `kSlave` на `kMaster` (фронт
делает upload → сразу GET; при появлении реплики slave может отставать).
Если константа `kSlave` больше не используется — удалить её, чтобы не было
warning про unused.

## Фикс 8. Валидация max-size-bytes

`file_service.cpp`, конструктор — после инициализации:

```cpp
if (max_size_bytes_ <= 0) {
    throw std::runtime_error("max-size-bytes must be positive");
}
```

В static-config schema `minimum: 1` добавить ТОЛЬКО если MergeSchemas такое
принимает (проверить сборкой); если ругается — оставить только runtime-проверку.

## Фикс 9. Проверка Content-Type без учёта регистра

`ParseUploadRequest`: привести копию заголовка к lowercase перед `find`:

```cpp
std::string ct_lc{ct_header};
for (char& c : ct_lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
if (ct_lc.find("multipart/form-data") == std::string::npos) { ... 415 ... }
```

## Что НЕ менять

- ACL (EnsureAccess), meta/download-логику доступа — не трогать.
- Миграции — не добавлять.
- libs/common, gateway — не трогать.
- Никаких новых зависимостей.

## Acceptance

1. Регресс: upload 3MB → 201, download owner/related → 200, stranger → 403,
   not-a-uuid → 404, 11MB → 413 too_large, без purpose/file → 400, JSON → 415.
2. Upload с кириллическим именем `чек январь.pdf` → download отдаёт
   `Content-Disposition: attachment; filename="___ ______.pdf"; filename*=UTF-8''%D1%87%D0%B5%D0%BA%20...`
   (одна строка,両 параметра; браузер сохранит кириллическое имя).
3. Upload с Content-Type части, содержащим мусор длиной >255 → в meta
   `application/octet-stream`.
4. Имя файла длиной >255 байт (кириллица) → сохраняется усечённым, без битого
   последнего символа (валидный UTF-8 в JSON meta).
5. `MULTIPART/FORM-DATA` в заголовке (верхний регистр) → upload проходит.
6. Отрицательный FILE_MAX_SIZE_BYTES → сервис не стартует с понятной ошибкой
   (проверить и вернуть обратно).
7. Консистентность: вручную удалить блоб из тома
   (`docker compose exec file-service rm /data/files/<storage_key>`) →
   download → 500 internal envelope (не 404).
8. В логах при симуляции падения insert — сообщение про cleanup (проверять
   не обязательно, достаточно code review этой ветки try/catch).

## Как проверить

```bash
docker compose build file-service && docker compose up -d
BIG=/tmp/big.bin; dd if=/dev/urandom of=$BIG bs=1M count=3

curl -s -X POST http://localhost:8080/files -H "Authorization: Bearer $STUDENT1" \
  -F "purpose=payment_receipt" -F "file=@$BIG;filename=чек январь.pdf" | tee /tmp/up.json
FID=$(jq -r .id </tmp/up.json)
curl -sD - -o /dev/null http://localhost:8080/files/$FID/download \
  -H "Authorization: Bearer $STUDENT1" | grep -i content-disposition

# потеря блоба -> 500
KEY=$(docker compose exec -T postgres psql -U tutorflow -d file_db -tAc \
  "SELECT storage_key FROM files WHERE id='$FID'") 2>/dev/null || true
# (если имя БД/пользователь другие — подсмотреть в .env)
docker compose exec file-service rm "/data/files/$KEY"
curl -i http://localhost:8080/files/$FID/download -H "Authorization: Bearer $STUDENT1"  # 500

# регресс-набор из docs/tasks/file-service-hardening.md прогнать целиком
```

## Definition of Done

1. Два коммита в ветке: (а) этапы 1–2, (б) этот этап.
2. Сборка, /health, полный регресс + новые acceptance.
3. Ошибки в envelope.
4. Отчёт: изменённые файлы, что сделано, что намеренно нет.
