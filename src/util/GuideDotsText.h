#pragma once

#include <string>

// Guide Dots for text that is measured and drawn as plain strings: TXT pages and the speed reader.
// EPUB places its dots during layout (ParsedText) instead; both use the same middle dot (U+00B7)
// so the reading aid looks the same everywhere.
namespace GuideDotsText {

constexpr char DOT_UTF8[] = "\xc2\xb7";
// Replaces the space between two words.
constexpr char SEPARATOR[] = " \xc2\xb7 ";

// Returns `line` with a middle dot centered in every gap between two words: each run of spaces or
// tabs that has a word on both sides becomes " · ". Leading and trailing whitespace is kept, so
// indentation still lines up.
std::string withDots(const std::string& line);

}  // namespace GuideDotsText
