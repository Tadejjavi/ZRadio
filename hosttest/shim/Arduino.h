#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <ctime>
using std::min; using std::max;
#define DEG_TO_RAD 0.017453292519943295f
template<class T, class A, class B> static inline T constrain(T v, A lo, B hi) { return v < (T)lo ? (T)lo : v > (T)hi ? (T)hi : v; }
uint32_t millis();
class String {
public:
    std::string s;
    String() {}
    String(const char* c) : s(c ? c : "") {}
    String(const std::string& x) : s(x) {}
    String(int v) : s(std::to_string(v)) {}
    operator const char*() const { return s.c_str(); }
    const char* c_str() const { return s.c_str(); }
    unsigned length() const { return s.size(); }
    int indexOf(const char* t) const { auto p = s.find(t); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(char t) const { auto p = s.find(t); return p == std::string::npos ? -1 : (int)p; }
    String substring(int a) const { return a >= (int)s.size() ? String() : String(s.substr(a)); }
    String substring(int a, int b) const { if (a >= (int)s.size()) return String(); return String(s.substr(a, b - a)); }
    void toUpperCase() { for (auto& c : s) c = toupper((unsigned char)c); }
    void toLowerCase() { for (auto& c : s) c = tolower((unsigned char)c); }
    bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
    void trim() { while (s.size() && s.back() == ' ') s.pop_back(); while (s.size() && s[0] == ' ') s.erase(0, 1); }
    char operator[](int i) const { return s[i]; }
    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(const char* o) { s += o; return *this; }
    bool operator==(const String& o) const { return s == o.s; }
    bool operator!=(const String& o) const { return s != o.s; }
    bool operator==(const char* o) const { return s == o; }
    bool operator!=(const char* o) const { return s != o; }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
