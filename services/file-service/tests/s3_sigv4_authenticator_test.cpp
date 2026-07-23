#include <ctime>
#include <string>

#include <gtest/gtest.h>
#include <userver/clients/http/request.hpp>
#include <userver/crypto/hash.hpp>
#include <userver/s3api/models/request.hpp>
#include <userver/s3api/models/secret.hpp>

#include "storages/s3_sigv4_authenticator.hpp"

namespace tutorflow::file {
namespace {

SigV4Authenticator MakeMinioAuthenticator() {
    return SigV4Authenticator{"access", userver::s3api::Secret{"secret"},
                              "us-east-1", "minio:9000",
                              [] { return std::time_t{1369353600}; }};
}

TEST(SigV4Authenticator, MatchesAwsGetObjectVector) {
    userver::s3api::Request request;
    request.method = userver::clients::http::HttpMethod::kGet;
    request.bucket = "examplebucket";
    request.req = "test.txt";
    request.headers[std::string{"Range"}] = "bytes=0-9";

    SigV4Authenticator auth{
        "AKIAIOSFODNN7EXAMPLE",
        userver::s3api::Secret{"wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"},
        "us-east-1", "s3.amazonaws.com",
        [] { return std::time_t{1369353600}; }};

    const auto headers = auth.Auth(request);
    EXPECT_EQ(headers.at("Host"), "examplebucket.s3.amazonaws.com");
    EXPECT_EQ(headers.at("x-amz-date"), "20130524T000000Z");
    EXPECT_EQ(headers.at("x-amz-content-sha256"),
              "e3b0c44298fc1c149afbf4c8996fb924"
              "27ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(
        headers.at("Authorization"),
        "AWS4-HMAC-SHA256 "
        "Credential=AKIAIOSFODNN7EXAMPLE/20130524/us-east-1/s3/"
        "aws4_request,SignedHeaders=host;range;x-amz-content-sha256;"
        "x-amz-date,Signature="
        "f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41");
}

TEST(SigV4Authenticator, UsesEndpointHostForPathStyle) {
    userver::s3api::Request request;
    request.method = userver::clients::http::HttpMethod::kGet;
    request.req = "tutorflow-files/a%20b.txt";

    const auto headers = MakeMinioAuthenticator().Auth(request);
    EXPECT_EQ(headers.at("Host"), "minio:9000");
}

TEST(SigV4Authenticator, HashesPutBody) {
    userver::s3api::Request request;
    request.method = userver::clients::http::HttpMethod::kPut;
    request.req = "tutorflow-files/file";
    request.body = "abc";

    const auto headers = MakeMinioAuthenticator().Auth(request);
    EXPECT_EQ(headers.at("x-amz-content-sha256"),
              userver::crypto::hash::Sha256("abc"));
}

TEST(SigV4Authenticator, SortsEncodedQueryParameters) {
    userver::s3api::Request first;
    first.method = userver::clients::http::HttpMethod::kGet;
    first.req = "tutorflow-files/file?z=2&a=1";

    auto second = first;
    second.req = "tutorflow-files/file?a=1&z=2";

    EXPECT_EQ(MakeMinioAuthenticator().Auth(first).at("Authorization"),
              MakeMinioAuthenticator().Auth(second).at("Authorization"));
}

TEST(SigV4Authenticator, RejectsPresignedUrls) {
    EXPECT_THROW(MakeMinioAuthenticator().Sign({}, 0), std::logic_error);
}

} // namespace
} // namespace tutorflow::file
