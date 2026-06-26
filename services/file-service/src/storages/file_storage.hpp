#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <userver/clients/http/client.hpp>
#include <userver/components/component_context.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/components/loggable_component_base.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/yaml_config/schema.hpp>

namespace tutorflow::file {

class IFileStorage {
public:
    virtual ~IFileStorage() = default;

    virtual void Put(const std::string& storage_key,
                     std::string bytes,
                     const std::string& content_type) const = 0;

    virtual std::string Get(const std::string& storage_key) const = 0;

    virtual void Delete(const std::string& storage_key) const = 0;
};

class LocalFileStorage final : public IFileStorage {
public:
    LocalFileStorage(std::string storage_dir,
                     userver::engine::TaskProcessor& fs_tp);

    void Put(const std::string& storage_key,
             std::string bytes,
             const std::string& content_type) const override;

    std::string Get(const std::string& storage_key) const override;

    void Delete(const std::string& storage_key) const override;

private:
    std::string StoragePath(const std::string& storage_key) const;

    std::string storage_dir_;
    userver::engine::TaskProcessor& fs_tp_;
};

struct S3FileStorageSettings {
    std::string endpoint;
    std::string access_key;
    std::string secret_key;
    std::string bucket;
    std::string region;
};

class S3FileStorage final : public IFileStorage {
public:
    S3FileStorage(userver::clients::http::Client& http_client,
                  S3FileStorageSettings settings,
                  std::chrono::milliseconds timeout);

    void Put(const std::string& storage_key,
             std::string bytes,
             const std::string& content_type) const override;

    std::string Get(const std::string& storage_key) const override;

    void Delete(const std::string& storage_key) const override;

private:
    void EnsureBucketExists() const;
    std::string ObjectPath(const std::string& storage_key) const;
    std::string Url(std::string_view path) const;

    userver::clients::http::Client& http_client_;
    S3FileStorageSettings settings_;
    std::chrono::milliseconds timeout_;
    std::string host_;
};

class FileStorageComponent final
    : public userver::components::LoggableComponentBase,
      public IFileStorage {
public:
    static constexpr std::string_view kName = "file-storage";

    FileStorageComponent(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

    static userver::yaml_config::Schema GetStaticConfigSchema();

    void Put(const std::string& storage_key,
             std::string bytes,
             const std::string& content_type) const override;

    std::string Get(const std::string& storage_key) const override;

    void Delete(const std::string& storage_key) const override;

private:
    std::unique_ptr<IFileStorage> impl_;
};

}  // namespace tutorflow::file
