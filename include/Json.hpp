// Json.hpp - tiny helpers for building JSON text by hand (no external library).
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "Card.hpp"

namespace casino {
namespace json {

inline std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out += '"';
    return out;
}

// Joins already-encoded JSON values into an array.
inline std::string array(const std::vector<std::string>& encoded) {
    std::string out = "[";
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (i) out += ',';
        out += encoded[i];
    }
    return out + "]";
}

inline std::string strings(const std::vector<std::string>& items) {
    std::vector<std::string> encoded;
    encoded.reserve(items.size());
    for (const std::string& s : items) encoded.push_back(quote(s));
    return array(encoded);
}

inline std::string cards(const std::vector<Card>& hand) {
    std::vector<std::string> encoded;
    for (const Card& c : hand) encoded.push_back(quote(c.str()));
    return array(encoded);
}

inline std::string boolean(bool value) { return value ? "true" : "false"; }

inline std::string number(double value, int decimals = 3) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    return buf;
}

}  // namespace json
}  // namespace casino
