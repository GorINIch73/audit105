#pragma once

#include <string>
#include <vector>

// UTF-8 helpers: every string in the app is UTF-8, so byte-level ASCII
// routines (tolower, trim, find) must not be used on user text.

int Utf8Decode(const std::string& text, size_t& index);
std::string Utf8Encode(int codepoint);
size_t Utf8CodepointCount(const std::string& text);

bool IsUtf8Space(int codepoint);
std::string TrimUtf8(const std::string& text);
std::string CollapseSpacesUtf8(const std::string& text);

std::string ToLowerUtf8(const std::string& text);
std::string ToUpperUtf8(const std::string& text);
std::string NormalizeTextUtf8(const std::string& text);

bool ContainsUtf8(const std::string& haystack, const std::string& needle);
bool EqualsUtf8Insensitive(const std::string& a, const std::string& b);

bool MatchesFilter(const std::string& filter, const std::vector<std::string>& fields);
