#include "domain/file_service.hpp"

#include <exception>
#include <stdexcept>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/uuid4.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>
#include <tutorflow/clients/identity_grpc_client.hpp>

#include "repositories/file_repository.hpp"
#include "storages/file_storage.hpp"

namespace tutorflow::file {

FileService::FileService(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      repository_(context.FindComponent<FileRepository>()),
      identity_(context.FindComponent<tutorflow::clients::GrpcIdentityClient>()),
      storage_(context.FindComponent<FileStorageComponent>()),
      max_size_bytes_(config["max-size-bytes"].As<int64_t>(10 * 1024 * 1024)) {
    if (max_size_bytes_ <= 0) {
        throw std::runtime_error("max-size-bytes must be positive");
    }
}

// static
userver::yaml_config::Schema FileService::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<
        userver::components::LoggableComponentBase>(R"(
type: object
description: file domain service
additionalProperties: false
properties:
    max-size-bytes:
        type: integer
        minimum: 1
        description: upload size limit in bytes
        defaultDescription: '10485760'
)");
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
    const int64_t data_size       = static_cast<int64_t>(data.size());

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
}

FileMeta FileService::GetMetaUnchecked(const std::string& file_id) const {
    auto found = repository_.FindById(file_id);
    if (!found) {
        throw tutorflow::common::ServiceError::NotFound("file not found");
    }
    return *found;
}

void FileService::EnsureAccess(const FileMeta& meta,
                               const std::string& requester_id,
                               bool requester_is_teacher) const {
    if (meta.owner_user_id == requester_id) return;

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

FileMeta FileService::GetMeta(const std::string& file_id,
                              const std::string& requester_id,
                              bool requester_is_teacher) const {
    auto meta = GetMetaUnchecked(file_id);
    EnsureAccess(meta, requester_id, requester_is_teacher);
    return meta;
}

std::pair<FileMeta, std::string> FileService::Download(
    const std::string& file_id,
    const std::string& requester_id,
    bool requester_is_teacher) const {
    auto meta = GetMetaUnchecked(file_id);
    EnsureAccess(meta, requester_id, requester_is_teacher);

    std::string content = storage_.Get(meta.storage_key);

    return {std::move(meta), std::move(content)};
}

}  // namespace tutorflow::file
