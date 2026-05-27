#pragma once

#include <cpprest/http_listener.h>
#include <cpprest/http_msg.h>
#include <cpprest/uri.h>
#include <cpprest/asyncrt_utils.h>
#include <filesystem>

#define TOKEN "changeme"

class NxCacheService {
public:
    NxCacheService(const utility::string_t& url, const std::string& bearer_token, const std::filesystem::path& cache_dir);
	~NxCacheService() = default;

    void open() { listener.open().wait(); }
    void close() { listener.close().wait(); }

private:
    bool authenticate(web::http::http_request& request) const;
    void handle_get(web::http::http_request request);
    void handle_put(web::http::http_request request);
    bool is_valid_hash_filename(const std::string& hash) const;

    web::http::experimental::listener::http_listener listener;
    std::string bearer_token;
	std::filesystem::path cache_dir;
};
