#pragma once

#include <string>
#include <string_view>

#include <userver/s3api/clients/s3api.hpp>

#include "storages/file_storage.hpp"

namespace tutorflow::file {

enum class S3AddressingStyle { kPath, kVirtual };

class S3FileStorage final : public IFileStorage {
  public:
    S3FileStorage(userver::s3api::ClientPtr client, std::string bucket,
                  S3AddressingStyle addressing_style);

    void Put(const std::string& storage_key, std::string bytes,
             const std::string& content_type) const override;

    std::string Get(const std::string& storage_key) const override;

    void Delete(const std::string& storage_key) const override;

    void CheckReady() const override;

  private:
    std::string ObjectPath(std::string_view storage_key) const;
    std::string ReadinessPath() const;

    userver::s3api::ClientPtr client_;
    std::string bucket_;
    S3AddressingStyle addressing_style_;
};

} // namespace tutorflow::file
