#include "storages/s3_file_storage.hpp"

#include <optional>
#include <utility>

#include <userver/clients/http/error.hpp>
#include <userver/logging/log.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::file {

S3FileStorage::S3FileStorage(userver::s3api::ClientPtr client,
                             std::string bucket,
                             S3AddressingStyle addressing_style)
    : client_(std::move(client)), bucket_(std::move(bucket)),
      addressing_style_(addressing_style) {}

void S3FileStorage::Put(const std::string& storage_key, std::string bytes,
                        const std::string& content_type) const {
    try {
        client_->PutObject(
            ObjectPath(storage_key), std::move(bytes), std::nullopt,
            content_type.empty() ? "application/octet-stream" : content_type,
            std::nullopt, std::nullopt);
    } catch (const std::exception&) {
        LOG_ERROR() << "S3 put failed for bucket=" << bucket_
                    << " key=" << storage_key;
        throw tutorflow::common::ServiceError::Internal(
            "failed to put object to s3");
    }
}

std::string S3FileStorage::Get(const std::string& storage_key) const {
    try {
        return client_->TryGetObject(ObjectPath(storage_key), std::nullopt,
                                     nullptr);
    } catch (const userver::clients::http::HttpException& error) {
        if (error.code() == 404) {
            throw tutorflow::common::ServiceError::Internal(
                "storage object missing for existing metadata");
        }
        LOG_ERROR() << "S3 get failed for bucket=" << bucket_
                    << " key=" << storage_key;
        throw tutorflow::common::ServiceError::Internal(
            "failed to get object from s3");
    } catch (const std::exception&) {
        LOG_ERROR() << "S3 get failed for bucket=" << bucket_
                    << " key=" << storage_key;
        throw tutorflow::common::ServiceError::Internal(
            "failed to get object from s3");
    }
}

void S3FileStorage::Delete(const std::string& storage_key) const {
    try {
        client_->DeleteObject(ObjectPath(storage_key));
    } catch (const std::exception&) {
        LOG_ERROR() << "S3 delete failed for bucket=" << bucket_
                    << " key=" << storage_key;
        throw tutorflow::common::ServiceError::Internal(
            "failed to delete object from s3");
    }
}

void S3FileStorage::CheckReady() const {
    try {
        if (client_->GetObjectHead(ReadinessPath()))
            return;
    } catch (const std::exception&) {
    }
    throw tutorflow::common::ServiceError::Internal(
        "failed to check s3 bucket");
}

std::string S3FileStorage::ObjectPath(std::string_view storage_key) const {
    if (addressing_style_ == S3AddressingStyle::kVirtual) {
        return std::string{storage_key};
    }
    return bucket_ + "/" + std::string{storage_key};
}

std::string S3FileStorage::ReadinessPath() const {
    return addressing_style_ == S3AddressingStyle::kPath ? bucket_ : "";
}

} // namespace tutorflow::file
