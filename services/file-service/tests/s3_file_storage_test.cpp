#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <userver/clients/http/error.hpp>
#include <userver/s3api/utest/client_gmock.hpp>

#include <tutorflow/common/errors.hpp>

#include "storages/s3_file_storage.hpp"

namespace tutorflow::file {
namespace {

using testing::_;
using testing::Return;
using testing::StrictMock;
using testing::Throw;

using MockClient = StrictMock<userver::s3api::GMockClient>;

std::shared_ptr<MockClient> MakeClient() {
    return std::make_shared<MockClient>();
}

TEST(S3FileStorage, PathStylePrefixesBucketAndDefaultsContentType) {
    auto client = MakeClient();
    EXPECT_CALL(*client, PutObject("tutorflow-files/key", "bytes", _,
                                   "application/octet-stream", _, _))
        .WillOnce(Return("etag"));

    S3FileStorage{client, "tutorflow-files", S3AddressingStyle::kPath}.Put(
        "key", "bytes", "");
}

TEST(S3FileStorage, VirtualStyleKeepsRawKeyAndContentType) {
    auto client = MakeClient();
    EXPECT_CALL(*client,
                PutObject("folder/key", "bytes", _, "text/plain", _, _))
        .WillOnce(Return("etag"));

    S3FileStorage{client, "tutorflow-files", S3AddressingStyle::kVirtual}.Put(
        "folder/key", "bytes", "text/plain");
}

TEST(S3FileStorage, GetsAndDeletesMappedObjects) {
    auto client = MakeClient();
    EXPECT_CALL(*client, TryGetObject("bucket/key", _, nullptr, _))
        .WillOnce(Return("payload"));
    EXPECT_CALL(*client, DeleteObject("bucket/key"));

    S3FileStorage storage{client, "bucket", S3AddressingStyle::kPath};
    EXPECT_EQ(storage.Get("key"), "payload");
    storage.Delete("key");
}

TEST(S3FileStorage, MapsMissingObjectSeparately) {
    auto client = MakeClient();
    EXPECT_CALL(*client, TryGetObject("bucket/missing", _, nullptr, _))
        .WillOnce(Throw(userver::clients::http::HttpClientException{404, {}}));

    try {
        S3FileStorage{client, "bucket", S3AddressingStyle::kPath}.Get(
            "missing");
        FAIL() << "expected ServiceError";
    } catch (const tutorflow::common::ServiceError& error) {
        EXPECT_STREQ(error.what(),
                     "storage object missing for existing metadata");
    }
}

TEST(S3FileStorage, MapsProviderFailureToOperationError) {
    auto client = MakeClient();
    EXPECT_CALL(*client, DeleteObject("bucket/key"))
        .WillOnce(Throw(std::runtime_error{"provider response"}));

    try {
        S3FileStorage{client, "bucket", S3AddressingStyle::kPath}.Delete("key");
        FAIL() << "expected ServiceError";
    } catch (const tutorflow::common::ServiceError& error) {
        EXPECT_STREQ(error.what(), "failed to delete object from s3");
    }
}

TEST(S3FileStorage, ChecksPathAndVirtualBuckets) {
    auto path_client = MakeClient();
    EXPECT_CALL(*path_client, GetObjectHead("bucket", _))
        .WillOnce(Return(userver::s3api::Client::HeadersDataResponse{}));
    S3FileStorage{path_client, "bucket", S3AddressingStyle::kPath}.CheckReady();

    auto virtual_client = MakeClient();
    EXPECT_CALL(*virtual_client, GetObjectHead("", _))
        .WillOnce(Return(userver::s3api::Client::HeadersDataResponse{}));
    S3FileStorage{virtual_client, "bucket", S3AddressingStyle::kVirtual}
        .CheckReady();
}

TEST(S3FileStorage, FailsReadinessWhenBucketHeadFails) {
    auto client = MakeClient();
    EXPECT_CALL(*client, GetObjectHead("bucket", _))
        .WillOnce(Return(std::nullopt));

    EXPECT_THROW(
        S3FileStorage(client, "bucket", S3AddressingStyle::kPath).CheckReady(),
        tutorflow::common::ServiceError);
}

} // namespace
} // namespace tutorflow::file
