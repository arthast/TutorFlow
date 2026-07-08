# Задача: file-service — три фикса безопасности/корректности по итогам аудита

Дата постановки: 2026-07-07. Контекст собран координатором (Claude) по итогам
полного аудита file-service. Изменения согласованы координатором, включая
изменение поведения публичного `GET /files/{fileId}` (п.1).

## Ветка

Если ветка `feat/file-multipart-userver` ещё НЕ смержена в main — делать вторым
коммитом в ней же (работа по тому же сервису). Если уже смержена — новая ветка
`feat/file-service-hardening` от актуального main (`git pull --rebase origin main`).

## Фикс 1. Проверка доступа на GET метаданных (главный)

Проблема: `GetMetaHandler` (`GET /internal/files/{fileId}`, публично
`GET /files/{fileId}`) не проверяет доступ вообще — ни владельца, ни связь
teacher↔student. Любой залогиненный пользователь по чужому fileId получает
`owner_user_id`, `original_name`, `purpose`, `size_bytes`. При этом
`DownloadHandler` доступ проверяет. Проверено: другие сервисы
`/internal/files/{id}` не вызывают (только gateway) — ломать нечего.

Что сделать — та же симметричная проверка, что в `Download`:

1. В `FileService` вынести проверку доступа в приватный метод и использовать
   её и в `Download`, и в новом checked-варианте `GetMeta`:

```cpp
// file_service.hpp
FileMeta GetMeta(const std::string& file_id,
                 const std::string& requester_id,
                 bool requester_is_teacher) const;   // публичный, с проверкой

private:
    FileMeta GetMetaUnchecked(const std::string& file_id) const;
    void EnsureAccess(const FileMeta& meta,
                      const std::string& requester_id,
                      bool requester_is_teacher) const;
```

```cpp
// file_service.cpp
void FileService::EnsureAccess(const FileMeta& meta,
                               const std::string& requester_id,
                               bool requester_is_teacher) const {
    if (meta.owner_user_id == requester_id) return;
    const bool allowed =
        requester_is_teacher
            ? identity_.CheckAccess(requester_id, meta.owner_user_id).allowed
            : identity_.CheckAccess(meta.owner_user_id, requester_id).allowed;
    if (!allowed) {
        throw tutorflow::common::ServiceError::Forbidden(
            "access denied: no active teacher-student relation");
    }
}

FileMeta FileService::GetMeta(const std::string& file_id,
                              const std::string& requester_id,
                              bool requester_is_teacher) const {
    auto meta = GetMetaUnchecked(file_id);
    EnsureAccess(meta, requester_id, requester_is_teacher);
    return meta;
}
```

   `Download` переписать на `GetMetaUnchecked` + `EnsureAccess` (текущая inline
   проверка удаляется, поведение не меняется).

2. В `GetMetaHandler` добавить `ParseAuthContext(request)` (как в Download) и
   передавать `auth.user_id`, `auth.IsTeacher()` в `service_.GetMeta(...)`.

Gateway уже пробрасывает `X-User-Id`/`X-User-Roles` на meta-роут
(`FileMetaHandler` → `Authenticate` → `ProxyToUpstream(..., auth)`) — на стороне
gateway менять НИЧЕГО не нужно.

## Фикс 2. Невалидный UUID в fileId → 404 вместо 500

Проблема: `FindById` кастует `$1::uuid` в SQL; `GET /files/not-a-uuid` даёт
ошибку каста Postgres → generic `std::exception` → 500 internal, причём текст
SQL-ошибки утекает в envelope.

Что сделать: в `file_handlers.cpp` валидировать формат UUID у path-аргумента и
при несовпадении кидать `ServiceError::NotFound("file not found")` (404, а не
400 — не раскрываем, существует ли ресурс). Без новых зависимостей, простая
проверка формата 8-4-4-4-12 hex:

```cpp
bool IsUuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

std::string RequiredUuidPathArg(const http::HttpRequest& req,
                                std::string_view name) {
    auto v = RequiredPathArg(req, name);
    if (!IsUuid(v)) {
        throw ServiceError::NotFound("file not found");
    }
    return v;
}
```

Использовать `RequiredUuidPathArg(request, "fileId")` в `GetMetaHandler` и
`DownloadHandler`. Добавить `#include <cctype>` при необходимости.

## Фикс 3. Санитизация имени файла в Content-Disposition

Проблема: `DownloadHandler` вставляет `meta.original_name` (клиентский ввод при
загрузке) в заголовок как есть: `"attachment; filename=\"" + name + "\""`.
Кавычка ломает заголовок, CR/LF — потенциальный response splitting.

Что сделать: санитизировать имя перед подстановкой:

```cpp
std::string SanitizeDispositionFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) continue;  // управляющие, включая CR/LF
        if (c == '"' || c == '\\') { out.push_back('_'); continue; }
        out.push_back(static_cast<char>(c));
    }
    if (out.empty()) out = "upload";
    return out;
}
```

И в `DownloadHandler`:

```cpp
"attachment; filename=\"" + SanitizeDispositionFilename(meta.original_name) + "\""
```

Санитизировать на выдаче (download), а не при сохранении — в БД имя остаётся
оригинальным. RFC 5987 `filename*` НЕ внедрять — не over-engineering'уем.

## Что НЕ менять

- Upload-путь, multipart-парсинг, max_request_size — уже сделано, не трогать.
- Repository, storage, миграции, gateway — без изменений.
- Никаких новых зависимостей.
- Тексты/коды ошибок — только через `ServiceError` (envelope как везде).

## Контракт

Поведение `GET /files/{fileId}` меняется: добавляется 401/403 (как у download)
и честный 404 на невалидный id. Это согласовано координатором. Если в
`docs/api-contracts/file.openapi.yaml` у meta-эндпоинта перечислены ответы —
добавить туда 403 (файл локальный, не коммитится — правь спокойно).

## Acceptance

1. Владелец: `GET /files/{id}` своего файла → 200 (как раньше).
2. Связанный пользователь (teacher этого студента или наоборот): meta → 200.
3. Посторонний залогиненный: meta → 403, envelope `forbidden`.
4. Без `X-User-Id` (напрямую в internal, минуя gateway): meta → 401.
5. `GET /files/not-a-uuid` и `GET /files/not-a-uuid/download` → 404 envelope
   (сейчас 500).
6. Валидный UUID, но несуществующий → 404 (как раньше).
7. Загрузить файл с именем `evil".pdf` → download отдаёт корректный
   Content-Disposition без разрыва заголовка, имя с `_` вместо кавычки.
8. Регресс: download 3MB через gateway → 200; `Download` для владельца и
   связанного пользователя работает как раньше (403 для постороннего).

## Как проверить

```bash
docker compose build file-service && docker compose up -d
# Два пользователя: student1 (владелец), student2 (без связи), teacher1 (со связью).
# Токены получить как обычно через gateway login.

FID=$(curl -s -X POST http://localhost:8080/files \
  -H "Authorization: Bearer $STUDENT1" \
  -F "purpose=payment_receipt" -F 'file=@/tmp/big.bin;filename=evil".pdf' \
  | jq -r .id)   # если jq нет — глазами из ответа

curl -i http://localhost:8080/files/$FID -H "Authorization: Bearer $STUDENT1"  # 200
curl -i http://localhost:8080/files/$FID -H "Authorization: Bearer $TEACHER1"  # 200 (есть связь)
curl -i http://localhost:8080/files/$FID -H "Authorization: Bearer $STUDENT2"  # 403
curl -i http://localhost:8080/files/not-a-uuid -H "Authorization: Bearer $STUDENT1"          # 404
curl -i http://localhost:8080/files/not-a-uuid/download -H "Authorization: Bearer $STUDENT1" # 404
curl -sD - -o /dev/null http://localhost:8080/files/$FID/download \
  -H "Authorization: Bearer $STUDENT1" | grep -i content-disposition
# ожидание: attachment; filename="evil_.pdf" — одна строка, заголовки не разорваны
```

Дополнительно (рекомендуется, если быстро): `tests/test_files.py` в стиле
существующих e2e (`tests/_client.py` уже умеет multipart — см. `test_finance.py`):
кейсы 200-owner / 200-related / 403-stranger / 404-bad-uuid / download-регресс.
Если фикстуры под второго ученика без связи заводить долго — пропусти, curl
достаточно; отметь это в отчёте.

## Definition of Done

1. Сервис собирается, `/health` отвечает, стек поднимается.
2. Все пункты acceptance проходят через gateway.
3. Ошибки в едином envelope.
4. Перечислить изменённые файлы; отметить, что сделано и что намеренно нет.
