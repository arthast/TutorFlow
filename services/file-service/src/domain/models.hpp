#pragma once

#include <cstdint>
#include <string>

#include <userver/formats/json/value.hpp>

namespace tutorflow::file {

struct FileMeta {
    std::string id;
    std::string owner_user_id;
    std::string purpose;
    std::string original_name;
    std::string content_type;
    int64_t size_bytes{};
    std::string storage_key;
    std::string created_at;
};

userver::formats::json::Value ToJson(const FileMeta& m);

}  // namespace tutorflow::file
