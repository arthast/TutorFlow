#pragma once

#include <string_view>

#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>

namespace tutorflow::file {

class FileService;

class UploadHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "file-upload-handler";

    UploadHandler(const userver::components::ComponentConfig& config,
                  const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const FileService& service_;
};

class GetMetaHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "file-get-meta-handler";

    GetMetaHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const FileService& service_;
};

class DownloadHandler final
    : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "file-download-handler";

    DownloadHandler(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext&) const override;

private:
    const FileService& service_;
};

}  // namespace tutorflow::file
