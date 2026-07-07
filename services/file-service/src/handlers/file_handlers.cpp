#include "handlers/file_handlers.hpp"

#include <cctype>
#include <string>
#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/server/http/form_data_arg.hpp>
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

struct ParsedUpload {
    std::string purpose;
    std::string original_name;
    std::string content_type;
    std::string data;
};

ParsedUpload ParseUploadRequest(const http::HttpRequest& req) {
    const auto& ct_header = req.GetHeader("Content-Type");
    if (ct_header.find("multipart/form-data") == std::string::npos) {
        throw ServiceError(http::HttpStatus::kUnsupportedMediaType,
                           "unsupported_media_type",
                           "Content-Type must be multipart/form-data");
    }
    if (!req.HasFormDataArg("purpose")) {
        throw ServiceError::Validation("missing required field: purpose");
    }
    if (!req.HasFormDataArg("file")) {
        throw ServiceError::Validation("missing required field: file");
    }
    const auto& purpose_arg = req.GetFormDataArg("purpose");
    const auto& file_arg = req.GetFormDataArg("file");

    ParsedUpload result;
    result.purpose = std::string{purpose_arg.value};
    result.original_name =
        (file_arg.filename && !file_arg.filename->empty()) ? *file_arg.filename
                                                           : "upload";
    result.content_type = file_arg.content_type
                              ? std::string{*file_arg.content_type}
                              : "application/octet-stream";
    result.data = std::string{file_arg.value};

    static constexpr std::string_view kValidPurposes[] = {
        "assignment_attachment", "submission_file", "payment_receipt",
        "lesson_material", "chat_message"};
    bool valid_purpose = false;
    for (auto p : kValidPurposes) {
        if (result.purpose == p) { valid_purpose = true; break; }
    }
    if (!valid_purpose) {
        throw ServiceError::Validation("invalid purpose value");
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

bool IsUuid(std::string_view s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

std::string RequiredUuidPathArg(const http::HttpRequest& req,
                                std::string_view name) {
    auto v = RequiredPathArg(req, name);
    if (!IsUuid(v)) {
        throw ServiceError::NotFound("file not found");
    }
    return v;
}

std::string SanitizeDispositionFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) continue;
        if (c == '"' || c == '\\') {
            out.push_back('_');
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    if (out.empty()) out = "upload";
    return out;
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
        const auto auth = tutorflow::common::ParseAuthContext(request);
        return JsonResponse(
            request,
            ToJson(service_.GetMeta(
                RequiredUuidPathArg(request, "fileId"),
                auth.user_id,
                auth.IsTeacher())));
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
            RequiredUuidPathArg(request, "fileId"),
            auth.user_id,
            is_teacher);

        request.GetHttpResponse().SetStatus(http::HttpStatus::kOk);
        request.GetHttpResponse().SetHeader(std::string{"Content-Type"},
                                            meta.content_type);
        request.GetHttpResponse().SetHeader(
            std::string{"Content-Disposition"},
            "attachment; filename=\"" +
                SanitizeDispositionFilename(meta.original_name) + "\"");
        request.GetHttpResponse().SetHeader(
            std::string{"Content-Length"},
            std::to_string(content.size()));
        return content;
    });
}

}  // namespace tutorflow::file
