#include "handlers/file_handlers.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/server/http/http_request.hpp>

#include <tutorflow/common/auth_context.hpp>
#include <tutorflow/common/errors.hpp>
#include <tutorflow/common/handler_helpers.hpp>

#include "domain/file_service.hpp"
#include "domain/models.hpp"

namespace tutorflow::file {
namespace {
namespace http = userver::server::http;
using tutorflow::common::HandleEnvelope;
using tutorflow::common::JsonResponse;
using tutorflow::common::ServiceError;

// ---------------------------------------------------------------------------
// Multipart/form-data parser
// ---------------------------------------------------------------------------

struct MultipartField {
    std::string name;
    std::string filename;
    std::string content_type;
    std::string data;
};

// Extracts boundary value from "multipart/form-data; boundary=<val>" header.
std::string ExtractBoundary(std::string_view content_type) {
    const std::string kKey = "boundary=";
    auto pos = content_type.find(kKey);
    if (pos == std::string_view::npos) return {};
    pos += kKey.size();
    if (pos >= content_type.size()) return {};

    std::string_view rest = content_type.substr(pos);
    if (!rest.empty() && rest.front() == '"') {
        rest = rest.substr(1);
        auto end = rest.find('"');
        return std::string{rest.substr(0, end)};
    }
    // Unquoted: ends at ';' or whitespace or end
    auto end = rest.find_first_of("; \t\r\n");
    return std::string{rest.substr(0, end)};
}

// Extracts a named parameter from a Content-Disposition value.
// e.g. name="file" or filename="test.pdf"
std::optional<std::string> ExtractCDParam(std::string_view cd, std::string_view key) {
    const std::string search = std::string{key} + "=\"";
    auto pos = cd.find(search);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += search.size();
    auto end = cd.find('"', pos);
    if (end == std::string_view::npos) return std::nullopt;
    return std::string{cd.substr(pos, end - pos)};
}

std::vector<MultipartField> ParseMultipart(std::string_view body,
                                           const std::string& boundary) {
    std::vector<MultipartField> fields;
    const std::string delim = "--" + boundary;

    size_t pos = body.find(delim);
    if (pos == std::string_view::npos) return fields;
    pos += delim.size();

    while (pos < body.size()) {
        // After boundary: either "\r\n" (more parts) or "--" (end)
        if (pos + 1 < body.size() &&
            body[pos] == '-' && body[pos + 1] == '-') {
            break;
        }
        // Skip \r\n
        if (pos + 1 < body.size() &&
            body[pos] == '\r' && body[pos + 1] == '\n') {
            pos += 2;
        } else if (pos < body.size() && body[pos] == '\n') {
            pos += 1;
        } else {
            break;
        }

        // Find end of headers
        auto headers_end = body.find("\r\n\r\n", pos);
        if (headers_end == std::string_view::npos) {
            headers_end = body.find("\n\n", pos);
            if (headers_end == std::string_view::npos) break;
            const size_t data_start = headers_end + 2;
            // (simplified path for \n\n — unlikely in practice but safe)
            auto next = body.find(delim, data_start);
            if (next == std::string_view::npos) break;
            pos = next + delim.size();
            continue;
        }
        const std::string_view headers_sv = body.substr(pos, headers_end - pos);
        const size_t data_start = headers_end + 4;

        // Find next boundary
        auto next = body.find(delim, data_start);
        if (next == std::string_view::npos) break;

        // Data ends just before \r\n--boundary
        size_t data_end = next;
        if (data_end >= 2 && body[data_end - 2] == '\r' && body[data_end - 1] == '\n') {
            data_end -= 2;
        } else if (data_end >= 1 && body[data_end - 1] == '\n') {
            data_end -= 1;
        }

        MultipartField field;
        field.data = std::string{body.substr(data_start, data_end - data_start)};

        // Parse headers (case-insensitive search)
        std::string headers_str{headers_sv};
        // Lowercase copy for searching
        std::string headers_lc = headers_str;
        for (char& c : headers_lc) c = static_cast<char>(std::tolower(c));

        // Content-Disposition
        auto cd_pos = headers_lc.find("content-disposition:");
        if (cd_pos != std::string::npos) {
            auto cd_end = headers_str.find("\r\n", cd_pos);
            std::string_view cd = std::string_view{headers_str}.substr(
                cd_pos, cd_end == std::string::npos ? std::string::npos : cd_end - cd_pos);
            if (auto n = ExtractCDParam(cd, "name")) field.name = *n;
            if (auto f = ExtractCDParam(cd, "filename")) field.filename = *f;
        }

        // Content-Type
        auto ct_pos = headers_lc.find("content-type:");
        if (ct_pos != std::string::npos) {
            ct_pos += 13;
            while (ct_pos < headers_str.size() && headers_str[ct_pos] == ' ') ++ct_pos;
            auto ct_end = headers_str.find("\r\n", ct_pos);
            field.content_type = headers_str.substr(
                ct_pos, ct_end == std::string::npos ? std::string::npos : ct_end - ct_pos);
        }

        if (!field.name.empty()) fields.push_back(std::move(field));
        pos = next + delim.size();
    }
    return fields;
}

struct ParsedUpload {
    std::string purpose;
    std::string original_name;
    std::string content_type;
    std::string data;
};

ParsedUpload ParseUploadRequest(const http::HttpRequest& req) {
    const auto ct_header = req.GetHeader("Content-Type");
    if (ct_header.find("multipart/form-data") == std::string::npos) {
        throw ServiceError(http::HttpStatus::kUnsupportedMediaType,
                           "unsupported_media_type",
                           "Content-Type must be multipart/form-data");
    }
    const std::string boundary = ExtractBoundary(ct_header);
    if (boundary.empty()) {
        throw ServiceError::Validation("missing multipart boundary in Content-Type");
    }
    const auto fields = ParseMultipart(req.RequestBody(), boundary);

    ParsedUpload result;
    for (const auto& f : fields) {
        if (f.name == "file") {
            result.original_name = f.filename.empty() ? "upload" : f.filename;
            result.content_type  = f.content_type.empty()
                                       ? "application/octet-stream"
                                       : f.content_type;
            result.data = f.data;
        } else if (f.name == "purpose") {
            result.purpose = f.data;
        }
    }

    if (result.purpose.empty()) {
        throw ServiceError::Validation("missing required field: purpose");
    }
    static constexpr std::string_view kValidPurposes[] = {
        "assignment_attachment", "submission_file", "payment_receipt",
        "lesson_material"};
    bool valid_purpose = false;
    for (auto p : kValidPurposes) {
        if (result.purpose == p) { valid_purpose = true; break; }
    }
    if (!valid_purpose) {
        throw ServiceError::Validation("invalid purpose value");
    }
    if (result.data.empty() && result.original_name.empty()) {
        throw ServiceError::Validation("missing required field: file");
    }
    return result;
}

std::string RequiredPathArg(const http::HttpRequest& req, std::string_view name) {
    auto v = req.GetPathArg(std::string{name});
    if (v.empty()) {
        throw ServiceError::Validation("missing path parameter: " + std::string{name});
    }
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// UploadHandler
// ---------------------------------------------------------------------------

UploadHandler::UploadHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FileService>()) {}

std::string UploadHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        const auto auth = tutorflow::common::ParseAuthContext(request);
        const auto upload = ParseUploadRequest(request);
        return JsonResponse(
            request,
            ToJson(service_.Upload(auth.user_id, upload.purpose,
                                   upload.original_name, upload.content_type,
                                   upload.data)),
            http::HttpStatus::kCreated);
    });
}

// ---------------------------------------------------------------------------
// GetMetaHandler
// ---------------------------------------------------------------------------

GetMetaHandler::GetMetaHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FileService>()) {}

std::string GetMetaHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        return JsonResponse(
            request,
            ToJson(service_.GetMeta(RequiredPathArg(request, "fileId"))));
    });
}

// ---------------------------------------------------------------------------
// DownloadHandler
// ---------------------------------------------------------------------------

DownloadHandler::DownloadHandler(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : HttpHandlerBase(config, context),
      service_(context.FindComponent<FileService>()) {}

std::string DownloadHandler::HandleRequestThrow(
    const http::HttpRequest& request,
    userver::server::request::RequestContext&) const {
    return HandleEnvelope(request, [&] {
        const auto auth = tutorflow::common::ParseAuthContext(request);
        const bool is_teacher = auth.IsTeacher();

        auto [meta, content] = service_.Download(
            RequiredPathArg(request, "fileId"),
            auth.user_id,
            is_teacher);

        request.GetHttpResponse().SetStatus(http::HttpStatus::kOk);
        request.GetHttpResponse().SetHeader(std::string{"Content-Type"},
                                            meta.content_type);
        request.GetHttpResponse().SetHeader(
            std::string{"Content-Disposition"},
            "attachment; filename=\"" + meta.original_name + "\"");
        request.GetHttpResponse().SetHeader(
            std::string{"Content-Length"},
            std::to_string(content.size()));
        return content;
    });
}

}  // namespace tutorflow::file
