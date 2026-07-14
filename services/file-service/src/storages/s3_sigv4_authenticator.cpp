#include "storages/s3_sigv4_authenticator.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <userver/clients/http/request.hpp>
#include <userver/crypto/hash.hpp>
#include <userver/s3api/models/request.hpp>

namespace tutorflow::file {
namespace {

struct AwsDate {
    std::string date;
    std::string timestamp;
};

AwsDate FormatAwsDate(std::time_t time) {
    std::tm utc{};
    gmtime_r(&time, &utc);

    std::array<char, 9> date{};
    std::array<char, 17> timestamp{};
    std::strftime(date.data(), date.size(), "%Y%m%d", &utc);
    std::strftime(timestamp.data(), timestamp.size(), "%Y%m%dT%H%M%SZ", &utc);
    return {date.data(), timestamp.data()};
}

std::string ToLower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

std::string NormalizeHeaderValue(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool whitespace = false;
    for (const unsigned char ch : value) {
        if (std::isspace(ch)) {
            whitespace = !result.empty();
            continue;
        }
        if (whitespace)
            result.push_back(' ');
        result.push_back(static_cast<char>(ch));
        whitespace = false;
    }
    return result;
}

struct CanonicalTarget {
    std::string uri;
    std::string query;
};

CanonicalTarget MakeCanonicalTarget(std::string_view request_target) {
    const auto query_pos = request_target.find('?');
    const auto path = request_target.substr(0, query_pos);

    std::vector<std::string> query_params;
    if (query_pos != std::string_view::npos) {
        auto query = request_target.substr(query_pos + 1);
        while (!query.empty()) {
            const auto separator = query.find('&');
            query_params.emplace_back(query.substr(0, separator));
            if (separator == std::string_view::npos)
                break;
            query.remove_prefix(separator + 1);
        }
        std::sort(query_params.begin(), query_params.end());
    }

    std::string canonical_query;
    for (const auto& parameter : query_params) {
        if (!canonical_query.empty())
            canonical_query.push_back('&');
        canonical_query += parameter;
    }

    return {path.empty() ? "/" : "/" + std::string{path},
            std::move(canonical_query)};
}

struct CanonicalHeaders {
    std::string text;
    std::string names;
};

CanonicalHeaders MakeCanonicalHeaders(const userver::s3api::Request& request,
                                      std::string host,
                                      std::string payload_hash,
                                      std::string timestamp) {
    std::map<std::string, std::string> headers;
    for (const auto& [name, value] : request.headers) {
        headers[ToLower(name)] = NormalizeHeaderValue(value);
    }
    headers["host"] = std::move(host);
    headers["x-amz-content-sha256"] = std::move(payload_hash);
    headers["x-amz-date"] = std::move(timestamp);

    CanonicalHeaders result;
    for (const auto& [name, value] : headers) {
        result.text += name + ":" + value + "\n";
        if (!result.names.empty())
            result.names.push_back(';');
        result.names += name;
    }
    return result;
}

std::string HmacBinary(std::string_view key, std::string_view data) {
    return userver::crypto::hash::HmacSha256(
        key, data, userver::crypto::hash::OutputEncoding::kBinary);
}

} // namespace

SigV4Authenticator::SigV4Authenticator(std::string access_key,
                                       userver::s3api::Secret secret_key,
                                       std::string region,
                                       std::string endpoint_host, Now now)
    : access_key_(std::move(access_key)), secret_key_(std::move(secret_key)),
      region_(std::move(region)), endpoint_host_(std::move(endpoint_host)),
      now_(std::move(now)) {}

std::unordered_map<std::string, std::string>
SigV4Authenticator::Auth(const userver::s3api::Request& request) const {
    const auto aws_date = FormatAwsDate(now_());
    const auto payload_hash = userver::crypto::hash::Sha256(request.body);
    const auto host = request.bucket.empty()
                          ? endpoint_host_
                          : request.bucket + "." + endpoint_host_;
    const auto target = MakeCanonicalTarget(request.req);
    const auto canonical_headers =
        MakeCanonicalHeaders(request, host, payload_hash, aws_date.timestamp);

    const std::string canonical_request =
        std::string{userver::clients::http::ToStringView(request.method)} +
        "\n" + target.uri + "\n" + target.query + "\n" +
        canonical_headers.text + "\n" + canonical_headers.names + "\n" +
        payload_hash;
    const auto scope = aws_date.date + "/" + region_ + "/s3/aws4_request";
    const auto string_to_sign =
        "AWS4-HMAC-SHA256\n" + aws_date.timestamp + "\n" + scope + "\n" +
        userver::crypto::hash::Sha256(canonical_request);

    const auto date_key =
        HmacBinary("AWS4" + secret_key_.GetUnderlying(), aws_date.date);
    const auto region_key = HmacBinary(date_key, region_);
    const auto service_key = HmacBinary(region_key, "s3");
    const auto signing_key = HmacBinary(service_key, "aws4_request");
    const auto signature =
        userver::crypto::hash::HmacSha256(signing_key, string_to_sign);

    return {
        {"Host", host},
        {"x-amz-content-sha256", payload_hash},
        {"x-amz-date", aws_date.timestamp},
        {"Authorization", "AWS4-HMAC-SHA256 Credential=" + access_key_ + "/" +
                              scope +
                              ",SignedHeaders=" + canonical_headers.names +
                              ",Signature=" + signature},
    };
}

std::unordered_map<std::string, std::string>
SigV4Authenticator::Sign(const userver::s3api::Request&, std::time_t) const {
    throw std::logic_error{"presigned S3 URLs are not supported"};
}

} // namespace tutorflow::file
