#include <tutorflow/common/jwt.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <userver/formats/common/type.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::common::jwt {
namespace {

std::string Base64UrlEncode(const unsigned char* data, size_t len) {
    if (len == 0) return {};
    std::vector<unsigned char> buf(((len + 2) / 3) * 4 + 1, 0);
    int encoded_len =
        EVP_EncodeBlock(buf.data(), data, static_cast<int>(len));
    std::string result(reinterpret_cast<char*>(buf.data()),
                       static_cast<size_t>(encoded_len));
    for (char& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!result.empty() && result.back() == '=') result.pop_back();
    return result;
}

std::string Base64UrlEncodeStr(std::string_view s) {
    return Base64UrlEncode(
        reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

std::string Base64UrlDecode(std::string input) {
    for (char& c : input) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    size_t pad = (4 - input.size() % 4) % 4;
    input.append(pad, '=');
    std::vector<unsigned char> buf(input.size(), 0);
    int len = EVP_DecodeBlock(
        buf.data(),
        reinterpret_cast<const unsigned char*>(input.data()),
        static_cast<int>(input.size()));
    if (len < 0) return {};
    return std::string(reinterpret_cast<char*>(buf.data()),
                       static_cast<size_t>(len) - pad);
}

std::string HmacSha256B64Url(std::string_view data, std::string_view key) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &digest_len);
    return Base64UrlEncode(digest, digest_len);
}

constexpr std::string_view kHeader = R"({"alg":"HS256","typ":"JWT"})";

}  // namespace

std::string Sign(const Claims& claims, std::string_view secret) {
    namespace json = userver::formats::json;
    namespace common_formats = userver::formats::common;

    json::ValueBuilder payload_b;
    payload_b["sub"] = claims.sub;
    payload_b["exp"] = claims.exp;
    payload_b["iat"] = claims.iat;
    json::ValueBuilder roles_arr(common_formats::Type::kArray);
    for (const auto& r : claims.roles) roles_arr.PushBack(r);
    payload_b["roles"] = roles_arr.ExtractValue();
    const std::string payload_str =
        json::ToString(payload_b.ExtractValue());

    std::string h = Base64UrlEncodeStr(kHeader);
    std::string p = Base64UrlEncodeStr(payload_str);
    std::string signing_input = h + '.' + p;
    std::string sig = HmacSha256B64Url(signing_input, secret);
    return signing_input + '.' + sig;
}

std::optional<Claims> Verify(std::string_view token, std::string_view secret) {
    auto first_dot = token.find('.');
    if (first_dot == std::string_view::npos) return std::nullopt;
    auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string_view::npos) return std::nullopt;

    std::string_view header_p = token.substr(0, second_dot);
    std::string_view sig_b64 = token.substr(second_dot + 1);

    if (HmacSha256B64Url(std::string(header_p), secret) != sig_b64) {
        return std::nullopt;
    }

    std::string_view payload_b64 =
        token.substr(first_dot + 1, second_dot - first_dot - 1);
    std::string payload_str = Base64UrlDecode(std::string(payload_b64));
    if (payload_str.empty()) return std::nullopt;

    try {
        namespace json = userver::formats::json;
        auto json_val = json::FromString(payload_str);
        Claims claims;
        claims.sub = json_val["sub"].As<std::string>();
        claims.exp = json_val["exp"].As<int64_t>();
        claims.iat = json_val["iat"].As<int64_t>();
        for (const auto& r : json_val["roles"]) {
            claims.roles.push_back(r.As<std::string>());
        }
        auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        if (claims.exp < now_ts) return std::nullopt;
        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace tutorflow::common::jwt
