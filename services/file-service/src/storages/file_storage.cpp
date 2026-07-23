#include "storages/file_storage.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <userver/clients/http/component.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/engine/async.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/s3api/clients/s3api.hpp>
#include <userver/s3api/models/s3api_connection_type.hpp>
#include <userver/s3api/models/secret.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "storages/s3_file_storage.hpp"
#include "storages/s3_sigv4_authenticator.hpp"

namespace tutorflow::file {
namespace {
namespace json = userver::formats::json;

struct S3FileStorageSettings {
    std::string endpoint;
    std::string access_key;
    std::string secret_key;
    std::string bucket;
    std::string region;
    S3AddressingStyle addressing_style{S3AddressingStyle::kPath};
};

S3AddressingStyle ParseAddressingStyle(std::string_view value) {
    if (value == "path")
        return S3AddressingStyle::kPath;
    if (value == "virtual")
        return S3AddressingStyle::kVirtual;
    throw std::runtime_error(
        "s3 addressing_style must be either path or virtual");
}

S3FileStorageSettings ParseS3Settings(const json::Value& document) {
    const auto s3 = document["s3_file_storage"];
    return S3FileStorageSettings{
        s3["endpoint"].As<std::string>(""),
        s3["access_key"].As<std::string>(""),
        s3["secret_key"].As<std::string>(""),
        s3["bucket"].As<std::string>(""),
        s3["region"].As<std::string>("us-east-1"),
        ParseAddressingStyle(s3["addressing_style"].As<std::string>("path")),
    };
}

struct SecdistS3FileStorageSettings final : S3FileStorageSettings {
    explicit SecdistS3FileStorageSettings(const json::Value& document)
        : S3FileStorageSettings(ParseS3Settings(document)) {}
};

void ValidateS3Settings(const S3FileStorageSettings& settings) {
    if (settings.endpoint.empty() || settings.access_key.empty() ||
        settings.secret_key.empty() || settings.bucket.empty() ||
        settings.region.empty()) {
        throw std::runtime_error(
            "s3 file storage settings must include endpoint, access_key, "
            "secret_key, bucket and region");
    }
}

struct ParsedS3Endpoint {
    userver::s3api::S3ConnectionType connection_type;
    std::string host;
};

ParsedS3Endpoint ParseS3Endpoint(std::string endpoint) {
    userver::s3api::S3ConnectionType connection_type;
    std::size_t scheme_size = 0;
    if (endpoint.starts_with("http://")) {
        connection_type = userver::s3api::S3ConnectionType::kHttp;
        scheme_size = std::string_view{"http://"}.size();
    } else if (endpoint.starts_with("https://")) {
        connection_type = userver::s3api::S3ConnectionType::kHttps;
        scheme_size = std::string_view{"https://"}.size();
    } else {
        throw std::runtime_error(
            "s3 endpoint must start with http:// or https://");
    }

    std::string host = endpoint.substr(scheme_size);
    while (!host.empty() && host.back() == '/')
        host.pop_back();
    if (host.empty() || host.find_first_of("/?#") != std::string::npos) {
        throw std::runtime_error(
            "s3 endpoint must contain only scheme, host and optional port");
    }
    return {connection_type, std::move(host)};
}

} // namespace

LocalFileStorage::LocalFileStorage(std::string storage_dir,
                                   userver::engine::TaskProcessor& fs_tp)
    : storage_dir_(std::move(storage_dir)), fs_tp_(fs_tp) {}

void LocalFileStorage::Put(const std::string& storage_key, std::string bytes,
                           const std::string&) const {
    const std::string path = StoragePath(storage_key);
    userver::engine::AsyncNoTracing(fs_tp_, [path, data = std::move(bytes)]() {
        const std::filesystem::path final_path{path};
        const std::string temporary_path = path + ".tmp";
        std::filesystem::create_directories(final_path.parent_path());
        {
            std::ofstream output(temporary_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot open file for writing: " +
                                         temporary_path);
            }
            output.write(data.data(),
                         static_cast<std::streamsize>(data.size()));
            if (!output) {
                std::error_code error;
                std::filesystem::remove(temporary_path, error);
                throw std::runtime_error("failed to write file: " +
                                         temporary_path);
            }
        }
        std::filesystem::rename(temporary_path, final_path);
    }).Get();
}

std::string LocalFileStorage::Get(const std::string& storage_key) const {
    const std::string path = StoragePath(storage_key);
    return userver::engine::AsyncNoTracing(
               fs_tp_,
               [path]() -> std::string {
                   std::ifstream input(path, std::ios::binary);
                   if (!input) {
                       throw std::runtime_error("file not found on disk: " +
                                                path);
                   }
                   return std::string((std::istreambuf_iterator<char>(input)),
                                      std::istreambuf_iterator<char>());
               })
        .Get();
}

void LocalFileStorage::Delete(const std::string& storage_key) const {
    const std::string path = StoragePath(storage_key);
    userver::engine::AsyncNoTracing(fs_tp_, [path]() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }).Get();
}

void LocalFileStorage::CheckReady() const {}

std::string
LocalFileStorage::StoragePath(const std::string& storage_key) const {
    return storage_dir_ + "/" + storage_key;
}

FileStorageComponent::FileStorageComponent(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : LoggableComponentBase(config, context) {
    const auto backend = config["backend"].As<std::string>("local");
    if (backend == "local") {
        impl_ = std::make_unique<LocalFileStorage>(
            config["storage-dir"].As<std::string>(),
            context.GetTaskProcessor("fs-task-processor"));
        return;
    }
    if (backend == "s3") {
        const auto& secdist =
            context.FindComponent<userver::components::Secdist>().Get();
        auto settings = secdist.Get<SecdistS3FileStorageSettings>();
        ValidateS3Settings(settings);
        const auto endpoint = ParseS3Endpoint(settings.endpoint);
        const auto timeout =
            std::chrono::milliseconds{config["timeout-ms"].As<int>(5000)};
        auto connection = userver::s3api::MakeS3Connection(
            context.FindComponent<userver::components::HttpClient>()
                .GetHttpClient(),
            endpoint.connection_type, endpoint.host,
            userver::s3api::ConnectionCfg{timeout});
        auto authenticator = std::make_shared<SigV4Authenticator>(
            settings.access_key,
            userver::s3api::Secret{std::move(settings.secret_key)},
            settings.region, endpoint.host);
        const auto client_bucket =
            settings.addressing_style == S3AddressingStyle::kVirtual
                ? settings.bucket
                : std::string{};
        auto client = userver::s3api::GetS3Client(
            std::move(connection), std::move(authenticator), client_bucket);
        impl_ = std::make_unique<S3FileStorage>(std::move(client),
                                                std::move(settings.bucket),
                                                settings.addressing_style);
        return;
    }
    throw std::runtime_error("unsupported FILE_STORAGE_BACKEND: " + backend);
}

userver::yaml_config::Schema FileStorageComponent::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<
        userver::components::LoggableComponentBase>(R"(
type: object
description: file bytes storage
additionalProperties: false
properties:
    backend:
        type: string
        description: storage backend, local or s3
        defaultDescription: local
    storage-dir:
        type: string
        description: local directory for file storage (FILE_STORAGE_DIR)
    timeout-ms:
        type: integer
        description: S3 request timeout in milliseconds
        defaultDescription: '5000'
)");
}

void FileStorageComponent::Put(const std::string& storage_key,
                               std::string bytes,
                               const std::string& content_type) const {
    impl_->Put(storage_key, std::move(bytes), content_type);
}

std::string FileStorageComponent::Get(const std::string& storage_key) const {
    return impl_->Get(storage_key);
}

void FileStorageComponent::Delete(const std::string& storage_key) const {
    impl_->Delete(storage_key);
}

void FileStorageComponent::CheckReady() const { impl_->CheckReady(); }

} // namespace tutorflow::file
