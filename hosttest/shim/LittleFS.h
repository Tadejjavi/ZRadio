#pragma once
struct File { operator bool() const { return false; } size_t size() { return 0; } void read(uint8_t*, size_t) {} void close() {} };
struct LFS { File open(const char*, const char*) { return File(); } };
extern LFS LittleFS;
