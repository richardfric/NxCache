// https://nx.dev/docs/guides/tasks--caching/self-hosted-caching
#include "NxCache.h"
#include <cpprest/filestream.h>
#include <filesystem>

NxCacheService::NxCacheService(const utility::string_t& url, const std::string& bearer_token, const std::filesystem::path& cache_dir)
    : listener(url), bearer_token(bearer_token != TOKEN ? std::format("Bearer {}", bearer_token) : std::string()), cache_dir(cache_dir)
{
    listener.support(web::http::methods::GET, std::bind(&NxCacheService::handle_get, this, std::placeholders::_1));
    listener.support(web::http::methods::PUT, std::bind(&NxCacheService::handle_put, this, std::placeholders::_1));
}

bool NxCacheService::authenticate(web::http::http_request& request) const
{
	if (bearer_token.empty()) {
		return true; // No authentication if token is not set
	}
	const auto& headers = request.headers();
    auto auth = headers.find(U("authorization"));
    if (auth == headers.end()) {
        request.reply(web::http::status_codes::Unauthorized, U("Missing Authorization header."));
        return false;
    }
    if (utility::conversions::to_utf8string(auth->second) != bearer_token) {
        request.reply(web::http::status_codes::Unauthorized, U("Invalid or missing bearer token."));
        return false;
    }
    return true;
}

void NxCacheService::handle_get(web::http::http_request request)
{
    if (!authenticate(request)) {
        return;
    }
    auto paths = web::http::uri::split_path(web::http::uri::decode(request.relative_uri().path()));
    if (paths.size() != 3 || paths[0] != U("v1") || paths[1] != U("cache")) {
        request.reply(web::http::status_codes::NotFound);
        return;
    }
    auto hash = utility::conversions::to_utf8string(paths[2]);
    if (!is_valid_hash_filename(hash)) {
        request.reply(web::http::status_codes::Forbidden, "Invalid hash format.");
        return;
    }
    auto file_path = cache_dir / hash;
    if (!std::filesystem::exists(file_path)) {
        request.reply(web::http::status_codes::NotFound);
        return;
    }
    concurrency::streams::fstream::open_istream(utility::conversions::to_string_t(file_path.string()), std::ios::in | std::ios::binary)
        .then([request](concurrency::streams::istream file_stream) {
            web::http::http_response response(web::http::status_codes::OK);
            response.set_body(file_stream);
            response.headers().set_content_type(U("application/octet-stream"));
            return request.reply(response);
        })
        .then([request](pplx::task<void> previous_task) {
            try {
                previous_task.get(); // Will throw if any step above failed
            }
            catch (const std::exception& e) {
                request.reply(web::http::status_codes::InternalError, std::format("Failed to load the file:\n{}", e.what()));
            }
        });
}

void NxCacheService::handle_put(web::http::http_request request)
{
    if (!authenticate(request)) {
        return;
    }
    auto paths = web::http::uri::split_path(web::http::uri::decode(request.relative_uri().path()));
    if (paths.size() != 3 || paths[0] != U("v1") || paths[1] != U("cache")) {
        request.reply(web::http::status_codes::NotFound);
        return;
    }
    auto hash = utility::conversions::to_utf8string(paths[2]);
    if (!is_valid_hash_filename(hash)) {
        request.reply(web::http::status_codes::Forbidden, "Invalid hash format.");
        return;
    }
    auto file_path = cache_dir / hash;
    if (std::filesystem::exists(file_path)) {
        request.reply(web::http::status_codes::Conflict);
        return;
    }
    auto content_length_it = request.headers().find(U("Content-Length"));
    if (content_length_it == request.headers().end()) {
        request.reply(web::http::status_codes::BadRequest, U("Missing Content-Length header."));
        return;
    }
    size_t content_length = 0;
    try {
        size_t content_length_pos = 0;
        auto content_length_value = utility::conversions::to_utf8string(content_length_it->second);
        content_length = std::stoull(content_length_value, &content_length_pos);
        if (content_length_pos != content_length_value.size()) {
            request.reply(web::http::status_codes::BadRequest, U("Invalid Content-Length header."));
            return;
        }
    }
    catch (const std::exception&) {
        request.reply(web::http::status_codes::BadRequest, U("Invalid Content-Length header."));
        return;
    }
    concurrency::streams::fstream::open_ostream(utility::conversions::to_string_t(file_path.string()), std::ios::out | std::ios::binary)
        .then([request](concurrency::streams::ostream file_stream) {
            return request.body().read_to_end(file_stream.streambuf());
        })
        .then([request, file_path, content_length](size_t bytes_written) {
            if (bytes_written != content_length) {
                std::error_code ec;
                std::filesystem::remove(file_path, ec); // Clean up partial file
                request.reply(web::http::status_codes::BadRequest, U("Content-Length differs from stream size."));
            }
            else {
	            request.reply(web::http::status_codes::OK);
            }
        })
        .then([request, file_path](pplx::task<void> previous_task) {
            try {
                previous_task.get(); // Will throw if any step above failed
            }
            catch (const std::exception& e) {
                std::error_code ec;
                std::filesystem::remove(file_path, ec); // Clean up partial file
                request.reply(web::http::status_codes::InternalError, std::format("Failed to store the file:\n{}", e.what()));
            }
        });
}

bool NxCacheService::is_valid_hash_filename(const std::string& hash) const
{
    if (hash.empty())
        return false;

    // Allow only alphanumeric characters (a-z, A-Z, 0-9)
    return std::all_of(hash.begin(), hash.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}