#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/storages/postgres/cluster.hpp>

#include "domain/models.hpp"

namespace tutorflow::file {

class FileRepository final
    : public userver::components::LoggableComponentBase {
public:
    static constexpr std::string_view kName = "file-repository";

    FileRepository(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

    FileMeta SaveFileMeta(const std::string& owner_user_id,
                          const std::string& purpose,
                          const std::string& original_name,
                          const std::string& content_type,
                          int64_t size_bytes,
                          const std::string& storage_key) const;

    std::optional<FileMeta> FindById(const std::string& file_id) const;

private:
    userver::storages::postgres::ClusterPtr pg_;
};

}  // namespace tutorflow::file
