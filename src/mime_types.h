#pragma once
/**
 * MIME类型查找
 * 根据文件扩展名返回Content-Type
 */

#include <string>
#include <unordered_map>

namespace mime {

inline const std::unordered_map<std::string, std::string>& typeTable() {
    static const std::unordered_map<std::string, std::string> table = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".txt",  "text/plain"},
        {".xml",  "text/xml"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".bmp",  "image/bmp"},
        {".ico",  "image/x-icon"},
        {".svg",  "image/svg+xml"},
        {".mp3",  "audio/mpeg"},
        {".mp4",  "video/mp4"},
        {".pdf",  "application/pdf"},
        {".zip",  "application/zip"},
        {".gz",   "application/gzip"},
        {".c",    "text/plain"},
        {".cpp",  "text/plain"},
        {".h",    "text/plain"},
    };
    return table;
}

/** 根据扩展名查MIME类型，未知类型返回application/octet-stream */
inline std::string getType(const std::string& ext) {
    auto& table = typeTable();
    auto it = table.find(ext);
    if (it != table.end()) return it->second;
    return "application/octet-stream";
}

} // namespace mime
