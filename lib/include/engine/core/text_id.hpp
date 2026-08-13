#pragma once

#include <string>

namespace pac::core {

/// Return `explicit_id` when supplied; otherwise derive a readable deterministic
/// id from the inline source-language text. The slug is capped for practical
/// filenames and carries a short suffix to distinguish punctuation/long variants.
[[nodiscard]] std::string text_id(const std::string& explicit_id, const std::string& source_text);

} // namespace pac::core
