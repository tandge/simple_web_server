#include "http_request.h"

#include <algorithm>
#include <cstdlib>

bool HttpRequest::keepAlive() const {
    auto it = headers.find("Connection");
    if (it == headers.end()) {
        // HTTP/1.1默认Keep-Alive
        return version == HttpVersion::HTTP_11;
    }
    const std::string& val = it->second;
    return (val == "keep-alive" || val == "Keep-Alive");
}

HttpRequestParser::Result HttpRequestParser::parse(const char* data, size_t len) {
    buf_.append(data, len);

    while (true) {
        if (state_ == LINE) {
            size_t pos = buf_.find("\r\n");
            if (pos == std::string::npos) {
                return {INCOMPLETE, 0};
            }
            if (!parseRequestLine(buf_.substr(0, pos))) {
                return {PARSE_ERROR, 0};
            }
            buf_.erase(0, pos + 2);
            state_ = HEADERS;
        }

        if (state_ == HEADERS) {
            size_t pos = buf_.find("\r\n\r\n");
            if (pos == std::string::npos) {
                return {INCOMPLETE, 0};
            }

            // 逐行解析头部
            size_t start = 0;
            while (start < pos) {
                size_t line_end = buf_.find("\r\n", start);
                if (line_end > pos) break;
                if (line_end == start) {
                    // 空行，头部结束
                    break;
                }
                if (!parseHeaderLine(buf_.substr(start, line_end - start))) {
                    return {PARSE_ERROR, 0};
                }
                start = line_end + 2;
            }

            buf_.erase(0, pos + 4);

            // POST需要读body
            if (request_.method == HttpMethod::POST) {
                body_needed_ = findContentLength();
                if (body_needed_ == 0) {
                    return {PARSE_ERROR, 0}; // POST没有Content-Length
                }
                state_ = BODY;
            } else {
                state_ = DONE;
                return {COMPLETE, 0}; // consumed已在buf_中处理
            }
        }

        if (state_ == BODY) {
            if (buf_.size() < body_needed_) {
                return {INCOMPLETE, 0};
            }
            request_.body = buf_.substr(0, body_needed_);
            buf_.erase(0, body_needed_);
            state_ = DONE;
            return {COMPLETE, 0};
        }

        if (state_ == DONE) {
            return {COMPLETE, 0};
        }
    }
}

void HttpRequestParser::reset() {
    state_ = LINE;
    request_ = HttpRequest();
    buf_.clear();
    body_needed_ = 0;
}

bool HttpRequestParser::parseRequestLine(const std::string& line) {
    // 格式: METHOD /path HTTP/1.x
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;

    std::string method_str = line.substr(0, sp1);
    std::string uri = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string ver = line.substr(sp2 + 1);

    // 解析方法
    if (method_str == "GET") {
        request_.method = HttpMethod::GET;
    } else if (method_str == "POST") {
        request_.method = HttpMethod::POST;
    } else if (method_str == "HEAD") {
        request_.method = HttpMethod::HEAD;
    } else {
        return false;
    }

    // 解析URI（分离path和query）
    size_t qpos = uri.find('?');
    if (qpos != std::string::npos) {
        request_.path = uri.substr(0, qpos);
        request_.query = uri.substr(qpos + 1);
    } else {
        request_.path = uri;
    }

    // 根路径默认index.html
    if (request_.path == "/") {
        request_.path = "/index.html";
    }

    // 解析版本
    if (ver == "HTTP/1.0") {
        request_.version = HttpVersion::HTTP_10;
    } else if (ver == "HTTP/1.1") {
        request_.version = HttpVersion::HTTP_11;
    } else {
        return false;
    }

    return true;
}

bool HttpRequestParser::parseHeaderLine(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;

    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);

    // 去掉值前导空格
    size_t start = val.find_first_not_of(' ');
    if (start != std::string::npos) {
        val = val.substr(start);
    }

    request_.headers[key] = val;
    return true;
}

size_t HttpRequestParser::findContentLength() const {
    auto it = request_.headers.find("Content-Length");
    if (it == request_.headers.end()) {
        // 兼容小写
        it = request_.headers.find("Content-length");
    }
    if (it == request_.headers.end()) return 0;

    return static_cast<size_t>(std::atoi(it->second.c_str()));
}
