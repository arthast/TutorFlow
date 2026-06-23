#include "domain/file_service.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/engine/async.hpp>
#include <userver/utils/uuid4.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/clients/identity_client.hpp>

#include "repositories/file_repository.hpp"

namespace tutorflow::file {

FileService::FileService(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<FileRepository>()),
      identity_(context.FindComponent<tutorflow::clients::HttpIdentityClient>()),
      fs_tp_(context.GetTaskProcessor("fs-task-processor")),
      storage_dir_(config["storage-dir"].As<std::string>()),
      max_size_bytes_(config["max-size-bytes"].As<int64_t>(10 * 1024 * 1024)) {}

// static
userver::yaml_config::Schema FileService::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<
        userver::components::LoggableComponentBase>(R"(
type: object
description: file domain service
additionalProperties: false
properties:
    storage-dir:
        type: string
        description: local directory for file storage (FILE_STORAGE_DIR)
    max-size-bytes:
        type: integer
        description: upload size limit in bytes
        defaultDescription: '10485760'
)");
}

std::string FileService::StoragePath(const std::string& storage_key) const {
    return storage_dir_ + "/" + storage_key;
}

FileMeta FileService::Upload(const std::string& owner_user_id,
                             const std::string& purpose,
                             const std::string& original_name,
                             const std::string& content_type,
                             std::string data) const {
    if (static_cast<int64_t>(data.size()) > max_size_bytes_) {
        throw tutorflow::common::ServiceError(
            userver::server::http::HttpStatus::kPayloadTooLarge,
            "too_large",
            "file exceeds maximum allowed size");
    }

    const std::string storage_key = userver::utils::generators::GenerateUuid();
    const std::string path        = StoragePath(storage_key);
    const int64_t data_size       = static_cast<int64_t>(data.size());

    userver::engine::AsyncNoSpan(fs_tp_, [path, d = std::move(data)]() {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot open file for writing: " + path);
        }
        out.write(d.data(), static_cast<std::streamsize>(d.size()));
    }).Get();

    return repository_.SaveFileMeta(
        owner_user_id, purpose, original_name, content_type,
        data_size, storage_key);
}

FileMeta FileService::GetMeta(const std::string& file_id) const {
    auto found = repository_.FindById(file_id);
    if (!found) {
        throw tutorflow::common::ServiceError::NotFound("file not found");
    }
    return *found;
}

std::pair<FileMeta, std::string> FileService::Download(
    const std::string& file_id,
    const std::string& requester_id,
    bool requester_is_teacher) const {
    auto meta = GetMeta(file_id);

    const bool is_owner = (meta.owner_user_id == requester_id);
    if (!is_owner) {
        // Доступ симметричен: либо зовущий — преподаватель владельца-ученика,
        // либо зовущий — ученик владельца-преподавателя (вложение ДЗ, материалы
        // урока). В обоих случаях связь подтверждает identity check-access
        // (teacher, student).
        const bool allowed =
            requester_is_teacher
                ? identity_.CheckAccess(requester_id, meta.owner_user_id).allowed
                : identity_.CheckAccess(meta.owner_user_id, requester_id).allowed;
        if (!allowed) {
            throw tutorflow::common::ServiceError::Forbidden(
                "access denied: no active teacher-student relation");
        }
    }

    const std::string path = StoragePath(meta.storage_key);
    std::string content = userver::engine::AsyncNoSpan(
        fs_tp_, [path]() -> std::string {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                throw std::runtime_error("file not found on disk: " + path);
            }
            return std::string((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        }).Get();

    return {std::move(meta), std::move(content)};
}

}  // namespace tutorflow::file
