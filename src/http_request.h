#pragma once
/**
 * HTTP请求解析器
 * 支持HTTP/1.1 GET/POST/HEAD，增量解析
 */

#include <map>
#include <string>

enum class HttpMethod { GET, POST, HEAD };
enum class HttpVersion { HTTP_10, HTTP_11 };

struct HttpRequest {
    HttpMethod method = HttpMethod::GET;
    HttpVersion version = HttpVersion::HTTP_11;
    std::string path;
    std::string query;    // 问号后面的查询串
    std::map<std::string, std::string> headers;
    std::string body;

    /** 是否为Keep-Alive连接 */
    bool keepAlive() const;
};

class HttpRequestParser {
public:
    enum Code { COMPLETE, INCOMPLETE, PARSE_ERROR };

    struct Result {
        Code code;
        size_t consumed; // 从输入中消耗的字节数
    };

    /** 增量解析，返回解析结果和消耗字节数 */
    Result parse(const char* data, size_t len);

    const HttpRequest& request() const { return request_; }
    void reset();

private:
    bool parseRequestLine(const std::string& line);
    bool parseHeaderLine(const std::string& line);
    size_t findContentLength() const;

    enum State { LINE, HEADERS, BODY, DONE };
    State state_ = LINE;
    HttpRequest request_;
    std::string buf_;       // 暂存未完成的数据
    size_t body_needed_ = 0;
};
