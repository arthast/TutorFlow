#include "storages/file_storage.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <userver/clients/http/component.hpp>
#include <userver/clients/http/request.hpp>
#include <userver/clients/http/response.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/engine/async.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/secdist.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include <tutorflow/common/errors.hpp>

namespace tutorflow::file {
namespace {
namespace json = userver::formats::json;

constexpr std::string_view kSha256Empty =
    "e3b0c44298fc1c149afbf4c8996fb924"
    "27ae41e4649b934ca495991b7852b855";

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string ExtractHost(std::string_view endpoint) {
    auto host_begin = endpoint.find("://");
    host_begin = (host_begin == std::string_view::npos) ? 0 : host_begin + 3;
    auto host_end = endpoint.find('/', host_begin);
    return std::string{endpoint.substr(
        host_begin,
        host_end == std::string_view::npos ? std::string_view::npos
                                           : host_end - host_begin)};
}

std::string HexEncode(const unsigned char* data, std::size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = data[i];
        result.push_back(kHex[(byte >> 4) & 0x0F]);
        result.push_back(kHex[byte & 0x0F]);
    }
    return result;
}

std::string Sha256Hex(std::string_view data) {
    if (data.empty()) return std::string{kSha256Empty};

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_Digest(data.data(), data.size(), digest, &digest_len, EVP_sha256(),
               nullptr);
    return HexEncode(digest, digest_len);
}

std::string HmacSha256(std::string_view key, std::string_view data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &digest_len);
    return std::string(reinterpret_cast<char*>(digest), digest_len);
}

std::string HmacSha256Hex(std::string_view key, std::string_view data) {
    const auto digest = HmacSha256(key, data);
    return HexEncode(reinterpret_cast<const unsigned char*>(digest.data()),
                     digest.size());
}

struct AwsDate {
    std::string date;
    std::string date_time;
};

AwsDate NowAwsDate() {
    const auto now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);

    std::array<char, 17> date_time{};
    std::strftime(date_time.data(), date_time.size(), "%Y%m%dT%H%M%SZ", &utc);

    std::array<char, 9> date{};
    std::strftime(date.data(), date.size(), "%Y%m%d", &utc);
    return AwsDate{date.data(), date_time.data()};
}

bool IsUriUnreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string UriEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) {
        if (IsUriUnreserved(c)) {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(kHex[(c >> 4) & 0x0F]);
            result.push_back(kHex[c & 0x0F]);
        }
    }
    return result;
}

struct SignedHeaders {
    userver::clients::http::Headers headers;
};

SignedHeaders BuildSignedHeaders(const S3FileStorageSettings& settings,
                                 std::string_view host,
                                 std::string_view method,
                                 std::string_view canonical_uri,
                                 std::string_view payload,
                                 std::optional<std::string_view> content_type) {
    const auto aws_date = NowAwsDate();
    const std::string payload_hash = Sha256Hex(payload);

    std::vector<std::pair<std::string, std::string>> canonical_headers = {
        {"host", std::string{host}},
        {"x-amz-content-sha256", payload_hash},
        {"x-amz-date", aws_date.date_time},
    };
    if (content_type) {
        canonical_headers.emplace_back("content-type",
                                       std::string{*content_type});
    }
    std::sort(canonical_headers.begin(), canonical_headers.end());

    std::string canonical_headers_text;
    std::string signed_headers;
    for (const auto& [name, value] : canonical_headers) {
        canonical_headers_text += name + ":" + value + "\n";
        if (!signed_headers.empty()) signed_headers += ";";
        signed_headers += name;
    }

    const std::string canonical_request =
        std::string{method} + "\n" +
        std::string{canonical_uri} + "\n\n" +
        canonical_headers_text + "\n" +
        signed_headers + "\n" +
        payload_hash;

    const std::string credential_scope =
        aws_date.date + "/" + settings.region + "/s3/aws4_request";
    const std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" + aws_date.date_time + "\n" +
        credential_scope + "\n" + Sha256Hex(canonical_request);

    const auto date_key = HmacSha256("AWS4" + settings.secret_key, aws_date.date);
    const auto region_key = HmacSha256(date_key, settings.region);
    const auto service_key = HmacSha256(region_key, "s3");
    const auto signing_key = HmacSha256(service_key, "aws4_request");
    const auto signature = HmacSha256Hex(signing_key, string_to_sign);

    userver::clients::http::Headers headers;
    headers[std::string{"Host"}] = std::string{host};
    headers[std::string{"x-amz-content-sha256"}] = payload_hash;
    headers[std::string{"x-amz-date"}] = aws_date.date_time;
    if (content_type) {
        headers[userver::http::headers::kContentType] =
            std::string{*content_type};
    }
    headers[std::string{"Authorization"}] =
        "AWS4-HMAC-SHA256 Credential=" + settings.access_key + "/" +
        credential_scope + ", SignedHeaders=" + signed_headers +
        ", Signature=" + signature;

    return SignedHeaders{std::move(headers)};
}

S3FileStorageSettings ParseS3Settings(const json::Value& doc) {
    const auto s3 = doc["s3_file_storage"];
    return S3FileStorageSettings{
        s3["endpoint"].As<std::string>(""),
        s3["access_key"].As<std::string>(""),
        s3["secret_key"].As<std::string>(""),
        s3["bucket"].As<std::string>(""),
        s3["region"].As<std::string>("us-east-1"),
    };
}

struct SecdistS3FileStorageSettings final : S3FileStorageSettings {
    explicit SecdistS3FileStorageSettings(const json::Value& doc)
        : S3FileStorageSettings(ParseS3Settings(doc)) {}
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

std::string CreateBucketBody(std::string_view region) {
    if (region == "us-east-1") return {};
    return "<CreateBucketConfiguration "
           "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
           "<LocationConstraint>" +
           std::string{region} +
           "</LocationConstraint></CreateBucketConfiguration>";
}

}  // namespace

LocalFileStorage::LocalFileStorage(std::string storage_dir,
                                   userver::engine::TaskProcessor& fs_tp)
    : storage_dir_(std::move(storage_dir)), fs_tp_(fs_tp) {}

void LocalFileStorage::Put(const std::string& storage_key,
                           std::string bytes,
                           const std::string&) const {
    const std::string path = StoragePath(storage_key);
    userver::engine::AsyncNoTracing(fs_tp_, [path, d = std::move(bytes)]() {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("cannot open file for writing: " + path);
        }
        out.write(d.data(), static_cast<std::streamsize>(d.size()));
    }).Get();
}

std::string LocalFileStorage::Get(const std::string& storage_key) const {
    const std::string path = StoragePath(storage_key);
    return userver::engine::AsyncNoTracing(fs_tp_, [path]() -> std::string {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("file not found on disk: " + path);
        }
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }).Get();
}

void LocalFileStorage::Delete(const std::string& storage_key) const {
    const std::string path = StoragePath(storage_key);
    userver::engine::AsyncNoTracing(fs_tp_, [path]() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }).Get();
}

std::string LocalFileStorage::StoragePath(const std::string& storage_key) const {
    return storage_dir_ + "/" + storage_key;
}

S3FileStorage::S3FileStorage(userver::clients::http::Client& http_client,
                             S3FileStorageSettings settings,
                             std::chrono::milliseconds timeout)
    : http_client_(http_client),
      settings_(std::move(settings)),
      timeout_(timeout),
      host_(ExtractHost(settings_.endpoint)) {
    settings_.endpoint = TrimTrailingSlash(std::move(settings_.endpoint));
    ValidateS3Settings(settings_);
    EnsureBucketExists();
}

void S3FileStorage::Put(const std::string& storage_key,
                        std::string bytes,
                        const std::string& content_type) const {
    const auto path = ObjectPath(storage_key);
    const auto headers = BuildSignedHeaders(
        settings_, host_, "PUT", path, bytes,
        content_type.empty() ? std::optional<std::string_view>{"application/octet-stream"}
                             : std::optional<std::string_view>{content_type});
    const auto response = http_client_.CreateRequest()
                              .headers(headers.headers)
                              .timeout(timeout_)
                              .put(Url(path), std::move(bytes))
                              .perform();
    const auto status = static_cast<int>(response->status_code());
    if (status < 200 || status >= 300) {
        throw tutorflow::common::ServiceError::Internal(
            "failed to put object to s3: status " + std::to_string(status));
    }
}

std::string S3FileStorage::Get(const std::string& storage_key) const {
    const auto path = ObjectPath(storage_key);
    const auto headers = BuildSignedHeaders(settings_, host_, "GET", path, "",
                                            std::nullopt);
    const auto response = http_client_.CreateRequest()
                              .headers(headers.headers)
                              .timeout(timeout_)
                              .DisableReplyDecoding()
                              .get(Url(path))
                              .perform();
    const auto status = static_cast<int>(response->status_code());
    if (status == 404) {
        throw tutorflow::common::ServiceError::NotFound(
            "file object not found in storage");
    }
    if (status < 200 || status >= 300) {
        throw tutorflow::common::ServiceError::Internal(
            "failed to get object from s3: status " + std::to_string(status));
    }
    return response->body();
}

void S3FileStorage::Delete(const std::string& storage_key) const {
    const auto path = ObjectPath(storage_key);
    const auto headers = BuildSignedHeaders(settings_, host_, "DELETE", path, "",
                                            std::nullopt);
    const auto response = http_client_.CreateRequest()
                              .headers(headers.headers)
                              .timeout(timeout_)
                              .delete_method(Url(path))
                              .perform();
    const auto status = static_cast<int>(response->status_code());
    if (status < 200 || status >= 300) {
        throw tutorflow::common::ServiceError::Internal(
            "failed to delete object from s3: status " +
            std::to_string(status));
    }
}

void S3FileStorage::EnsureBucketExists() const {
    const auto bucket_path = "/" + UriEncode(settings_.bucket);
    {
        const auto headers = BuildSignedHeaders(settings_, host_, "HEAD",
                                                bucket_path, "", std::nullopt);
        const auto response = http_client_.CreateRequest()
                                  .headers(headers.headers)
                                  .timeout(timeout_)
                                  .head(Url(bucket_path))
                                  .perform();
        const auto status = static_cast<int>(response->status_code());
        if (status >= 200 && status < 300) return;
        if (status != 404) {
            throw tutorflow::common::ServiceError::Internal(
                "failed to check s3 bucket: status " +
                std::to_string(status));
        }
    }

    const auto body = CreateBucketBody(settings_.region);
    const auto headers = BuildSignedHeaders(
        settings_, host_, "PUT", bucket_path, body,
        body.empty() ? std::nullopt
                     : std::optional<std::string_view>{"application/xml"});
    const auto response = http_client_.CreateRequest()
                              .headers(headers.headers)
                              .timeout(timeout_)
                              .put(Url(bucket_path), body)
                              .perform();
    const auto status = static_cast<int>(response->status_code());
    if (status < 200 || status >= 300) {
        throw tutorflow::common::ServiceError::Internal(
            "failed to create s3 bucket: status " + std::to_string(status));
    }
}

std::string S3FileStorage::ObjectPath(const std::string& storage_key) const {
    return "/" + UriEncode(settings_.bucket) + "/" + UriEncode(storage_key);
}

std::string S3FileStorage::Url(std::string_view path) const {
    return settings_.endpoint + std::string{path};
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
        impl_ = std::make_unique<S3FileStorage>(
            context.FindComponent<userver::components::HttpClient>().GetHttpClient(),
            std::move(settings),
            std::chrono::milliseconds{
                config["timeout-ms"].As<int>(5000)});
        return;
    }
    throw std::runtime_error(
        "unsupported FILE_STORAGE_BACKEND: " + backend);
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

}  // namespace tutorflow::file
