#include "repositories/file_repository.hpp"

#include <userver/components/component_context.hpp>
#include <userver/storages/postgres/component.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::file {
namespace {
namespace pg = userver::storages::postgres;

constexpr auto kMaster = pg::ClusterHostType::kMaster;
constexpr auto kSlave  = pg::ClusterHostType::kSlave;

constexpr std::string_view kSelectFields = R"(
    id::text,
    owner_user_id::text,
    purpose,
    original_name,
    content_type,
    size_bytes,
    storage_key,
    to_char(created_at AT TIME ZONE 'UTC',
            'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at
)";

FileMeta RowToFileMeta(const pg::Row& row) {
    return FileMeta{
        row["id"].As<std::string>(),
        row["owner_user_id"].As<std::string>(),
        row["purpose"].As<std::string>(),
        row["original_name"].As<std::string>(),
        row["content_type"].As<std::string>(),
        row["size_bytes"].As<int64_t>(),
        row["storage_key"].As<std::string>(),
        row["created_at"].As<std::string>(),
    };
}

}  // namespace

FileRepository::FileRepository(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context),
      pg_(context.FindComponent<userver::components::Postgres>("file-db").GetCluster()) {}

FileMeta FileRepository::SaveFileMeta(const std::string& owner_user_id,
                                      const std::string& purpose,
                                      const std::string& original_name,
                                      const std::string& content_type,
                                      int64_t size_bytes,
                                      const std::string& storage_key) const {
    const auto result = pg_->Execute(
        kMaster,
        "INSERT INTO files (owner_user_id, purpose, original_name, content_type, "
        "                   size_bytes, storage_key) "
        "VALUES ($1::uuid, $2, $3, $4, $5, $6) "
        "RETURNING " + std::string{kSelectFields},
        owner_user_id, purpose, original_name, content_type, size_bytes, storage_key);
    if (result.IsEmpty()) {
        throw tutorflow::common::ServiceError::Internal("failed to save file metadata");
    }
    return RowToFileMeta(result[0]);
}

std::optional<FileMeta> FileRepository::FindById(const std::string& file_id) const {
    const auto result = pg_->Execute(
        kSlave,
        "SELECT " + std::string{kSelectFields} +
            " FROM files WHERE id = $1::uuid",
        file_id);
    if (result.IsEmpty()) return std::nullopt;
    return RowToFileMeta(result[0]);
}

}  // namespace tutorflow::file
