#include "http_response.h"

void HttpResponse::setStatusCode(int code) {
    status_code_ = code;
}

void HttpResponse::setStatusMessage(const std::string& msg) {
    status_msg_ = msg;
}

void HttpResponse::setContentType(const std::string& type) {
    headers_["Content-Type"] = type;
}

void HttpResponse::addHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::setBody(const std::string& body) {
    body_ = body;
}

void HttpResponse::setKeepAlive(bool on) {
    keep_alive_ = on;
}

std::string HttpResponse::serialize() const {
    std::string resp;

    // 状态行
    resp += "HTTP/1.1 " + std::to_string(status_code_) + " " + status_msg_ + "\r\n";

    // Connection
    if (keep_alive_) {
        resp += "Connection: keep-alive\r\n";
        resp += "Keep-Alive: timeout=300\r\n";
    } else {
        resp += "Connection: close\r\n";
    }

    // Content-Length
    resp += "Content-Length: " + std::to_string(body_.size()) + "\r\n";

    // Server
    resp += "Server: LightWebServer\r\n";

    // 其他头部
    for (auto& kv : headers_) {
        resp += kv.first + ": " + kv.second + "\r\n";
    }

    // 空行
    resp += "\r\n";

    // body
    resp += body_;

    return resp;
}

HttpResponse HttpResponse::makeError(int code, const std::string& msg) {
    HttpResponse resp;
    resp.setStatusCode(code);
    resp.setStatusMessage(msg);
    resp.setContentType("text/html");
    resp.setKeepAlive(false);

    std::string body = "<html><head><title>" + std::to_string(code) + " " + msg +
                       "</title></head><body><h1>" + std::to_string(code) +
                       " " + msg + "</h1></body></html>";
    resp.setBody(body);
    return resp;
}
