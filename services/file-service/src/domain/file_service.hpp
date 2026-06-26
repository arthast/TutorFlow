#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include "domain/models.hpp"

namespace tutorflow::clients {
class IdentityClient;
}

namespace tutorflow::file {

class FileRepository;
class IFileStorage;

class FileService final
    : public userver::components::LoggableComponentBase {
public:
    static constexpr std::string_view kName = "file-domain-service";

    FileService(const userver::components::ComponentConfig& config,
                const userver::components::ComponentContext& context);

    static userver::yaml_config::Schema GetStaticConfigSchema();

    FileMeta Upload(const std::string& owner_user_id,
                    const std::string& purpose,
                    const std::string& original_name,
                    const std::string& content_type,
                    std::string data) const;

    FileMeta GetMeta(const std::string& file_id) const;

    // Returns {meta, file_content}. Checks access for requester.
    std::pair<FileMeta, std::string> Download(
        const std::string& file_id,
        const std::string& requester_id,
        bool requester_is_teacher) const;

private:
    FileRepository& repository_;
    tutorflow::clients::IdentityClient& identity_;
    IFileStorage& storage_;
    int64_t max_size_bytes_{};
};

}  // namespace tutorflow::file
