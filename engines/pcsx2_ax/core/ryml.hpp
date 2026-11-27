#pragma once
#include <cstddef>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace ryml {
enum NodeType_e : unsigned int {
    NOTYPE = 0,
    MAP = 1,
    SEQ = 2,
    KEYVAL = 4,
    VAL = 8
};

struct csubstr {
    const char* str = nullptr;
    size_t len = 0;
    constexpr csubstr() = default;
    constexpr csubstr(const char* s) : str(s), len(s ? std::char_traits<char>::length(s) : 0) {}
    constexpr csubstr(const char* s, size_t n) : str(s), len(n) {}
    csubstr(const std::string& s) : str(s.data()), len(s.size()) {}
    bool empty() const { return !str || len == 0; }
    csubstr trimr(char) const { return *this; }
};

inline csubstr to_csubstr(std::string_view sv) { return csubstr(sv.data(), sv.size()); }
inline csubstr to_csubstr(const char* s) { return csubstr(s); }
inline csubstr to_csubstr(const std::string& s) { return csubstr(s); }
inline csubstr key(csubstr k) { return k; }

struct NodeRef {
    struct Iterator {
        bool operator!=(const Iterator&) const { return false; }
        void operator++() {}
        NodeRef operator*() const { return {}; }
    };
    struct Range {
        Iterator begin() const { return {}; }
        Iterator end() const { return {}; }
    };

    bool is_map() const { return false; }
    bool is_seq() const { return false; }
    bool has_child(csubstr) const { return false; }
    bool has_children() const { return false; }
    bool has_key_serialized() const { return false; }
    csubstr key() const { return {}; }
    csubstr val() const { return {}; }
    void set_key(csubstr) {}
    void remove_child(csubstr) {}
    NodeRef append_child(unsigned int = 0) { return {}; }
    NodeRef operator[](const char*) const { return {}; }
    NodeRef operator[](csubstr) const { return {}; }
    NodeRef operator[](size_t) const { return {}; }
    NodeRef& operator|=(unsigned int) { return *this; }
    template <typename T> NodeRef& operator=(const T&) { return *this; }
    template <typename T> NodeRef& operator<<(const T&) { return *this; }
    template <typename T> const NodeRef& operator>>(T& out) const { out = T{}; return *this; }
    template <typename T> NodeRef& operator>>(T& out) { out = T{}; return *this; }
    operator long long() const { return 0; }
    operator unsigned long long() const { return 0; }
    size_t num_children() const { return 0; }
    Iterator begin() const { return {}; }
    Iterator end() const { return {}; }
    Range children() const { return {}; }
};

struct Tree {
    bool empty() const { return true; }
    NodeRef rootref() { return {}; }
    NodeRef rootref() const { return {}; }
};

inline Tree parse_in_arena(csubstr) { return {}; }
inline Tree parse_in_arena(const char*) { return {}; }

inline csubstr emit_yaml(const Tree&) { return {}; }
inline csubstr emit_yaml(const Tree&, csubstr buf) { return buf; }
inline csubstr emit_yaml(const Tree&, std::string&) { return {}; }
inline csubstr emit_yaml(const Tree&, char*, size_t) { return {}; }
inline csubstr emit_yaml(const Tree&, const char*, size_t) { return {}; }
inline csubstr emit_yaml(const Tree&, NodeRef, std::string&) { return {}; }
inline csubstr emit_yaml(const Tree&, NodeRef, csubstr buf) { return buf; }
template <typename T1>
inline csubstr emit_yaml(const T1&) { return {}; }
template <typename T1, typename T2>
inline csubstr emit_yaml(const T1&, T2&&) { return {}; }
template <typename T1, typename T2, typename T3>
inline csubstr emit_yaml(const T1&, T2&&, T3&&) { return {}; }
} // namespace ryml

using ryml::emit_yaml;
inline bool SaveYAMLToFile(const char*, const ryml::Tree&) { return false; }
