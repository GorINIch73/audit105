#include "Utf8.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace {

int LowerCodepoint(int cp) {
    if (cp >= 0x41 && cp <= 0x5A) return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if (cp == 0x130) return 0x69;
    if (cp == 0x178) return 0xFF;
    if (cp >= 0x100 && cp <= 0x17F && (cp & 1) == 0) return cp + 1;
    if (cp >= 0x391 && cp <= 0x3A9) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x492 && cp <= 0x4A1 && (cp & 1) == 0) return cp + 1;
    return cp;
}

int UpperCodepoint(int cp) {
    if (cp >= 0x61 && cp <= 0x7A) return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    if (cp == 0x131) return 0x49;
    if (cp == 0xFF) return 0x178;
    if (cp >= 0x100 && cp <= 0x17F && (cp & 1) == 1) return cp - 1;
    if (cp >= 0x3B1 && cp <= 0x3C9) return cp - 32;
    if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
    if (cp >= 0x492 && cp <= 0x4A1 && (cp & 1) == 1) return cp - 1;
    return cp;
}

}  // namespace

int Utf8Decode(const std::string& text, size_t& index) {
    if (index >= text.size()) return 0;

    unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        ++index;
        return lead;
    }

    int expected = 0;
    int codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
        expected = 2;
        codepoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        expected = 3;
        codepoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        expected = 4;
        codepoint = lead & 0x07;
    } else {
        ++index;
        return 0xFFFD;
    }

    if (index + static_cast<size_t>(expected) > text.size()) {
        index = text.size();
        return 0xFFFD;
    }

    for (int i = 1; i < expected; ++i) {
        unsigned char next = static_cast<unsigned char>(text[index + i]);
        if ((next & 0xC0) != 0x80) {
            index += 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }

    index += static_cast<size_t>(expected);
    return codepoint;
}

std::string Utf8Encode(int codepoint) {
    std::string out;
    if (codepoint < 0) codepoint = 0;
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

size_t Utf8CodepointCount(const std::string& text) {
    size_t index = 0;
    size_t count = 0;
    while (index < text.size()) {
        Utf8Decode(text, index);
        ++count;
    }
    return count;
}

bool IsUtf8Space(int codepoint) {
    if (codepoint == 0x20 || codepoint == 0x09 || codepoint == 0x0A || codepoint == 0x0D) return true;
    if (codepoint == 0xA0) return true;
    if (codepoint >= 0x2000 && codepoint <= 0x200B) return true;
    if (codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x3000) return true;
    return false;
}

std::string TrimUtf8(const std::string& text) {
    std::vector<std::pair<size_t, int>> codepoints;
    codepoints.reserve(text.size());

    size_t index = 0;
    while (index < text.size()) {
        size_t start = index;
        int cp = Utf8Decode(text, index);
        codepoints.emplace_back(start, cp);
    }

    size_t first = 0;
    while (first < codepoints.size() && IsUtf8Space(codepoints[first].second)) ++first;
    size_t last = codepoints.size();
    while (last > first && IsUtf8Space(codepoints[last - 1].second)) --last;

    if (first >= last) return std::string();
    size_t beginByte = codepoints[first].first;
    size_t endByte = (last < codepoints.size()) ? codepoints[last].first : text.size();
    return text.substr(beginByte, endByte - beginByte);
}

std::string CollapseSpacesUtf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inSpace = false;

    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        if (IsUtf8Space(cp)) {
            if (!inSpace) out.push_back(' ');
            inSpace = true;
        } else {
            out += Utf8Encode(cp);
            inSpace = false;
        }
    }

    return TrimUtf8(out);
}

std::string ToLowerUtf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        out += Utf8Encode(LowerCodepoint(cp));
    }
    return out;
}

std::string ToUpperUtf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        out += Utf8Encode(UpperCodepoint(cp));
    }
    return out;
}

std::string NormalizeTextUtf8(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        int cp = Utf8Decode(text, index);
        if (cp == 0xAD || cp == 0xFEFF) continue;
        cleaned += Utf8Encode(LowerCodepoint(cp));
    }
    return CollapseSpacesUtf8(cleaned);
}

bool ContainsUtf8(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return ToLowerUtf8(haystack).find(ToLowerUtf8(needle)) != std::string::npos;
}

bool EqualsUtf8Insensitive(const std::string& a, const std::string& b) {
    return NormalizeTextUtf8(a) == NormalizeTextUtf8(b);
}

bool MatchesFilter(const std::string& filter, const std::vector<std::string>& fields) {
    if (filter.empty()) return true;
    std::vector<std::string> phrases;
    std::string phrase;
    for (char c : filter) {
        if (c == ',') {
            std::string trimmed = TrimUtf8(phrase);
            if (!trimmed.empty()) phrases.push_back(std::move(trimmed));
            phrase.clear();
        } else {
            phrase.push_back(c);
        }
    }
    std::string trimmed = TrimUtf8(phrase);
    if (!trimmed.empty()) phrases.push_back(std::move(trimmed));
    if (phrases.empty()) return true;

    for (const std::string& p : phrases) {
        bool hit = false;
        for (const std::string& field : fields) {
            if (ContainsUtf8(field, p)) {
                hit = true;
                break;
            }
        }
        if (!hit) return false;
    }
    return true;
}
