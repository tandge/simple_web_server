#pragma once
/**
 * HTTP响应构建器
 */

#include <map>
#include <string>

class HttpResponse {
public:
    void setStatusCode(int code);
    void setStatusMessage(const std::string& msg);
    void setContentType(const std::string& type);
    void addHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    void setKeepAlive(bool on);

    int statusCode() const { return status_code_; }
    const std::string& body() const { return body_; }

    /** 序列化为HTTP响应文本 */
    std::string serialize() const;

    /** 快捷构造错误响应 */
    static HttpResponse makeError(int code, const std::string& msg);

private:
    int status_code_ = 200;
    std::string status_msg_ = "OK";
    std::map<std::string, std::string> headers_;
    std::string body_;
    bool keep_alive_ = false;
};
