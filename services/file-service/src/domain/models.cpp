#include "domain/models.hpp"

#include <userver/formats/json/value_builder.hpp>

namespace tutorflow::file {

userver::formats::json::Value ToJson(const FileMeta& m) {
    userver::formats::json::ValueBuilder b;
    b["id"]            = m.id;
    b["owner_user_id"] = m.owner_user_id;
    b["purpose"]       = m.purpose;
    b["original_name"] = m.original_name;
    b["content_type"]  = m.content_type;
    b["size_bytes"]    = m.size_bytes;
    b["created_at"]    = m.created_at;
    return b.ExtractValue();
}

}  // namespace tutorflow::file
