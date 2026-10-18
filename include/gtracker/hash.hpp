#pragma once

#include <string>
#include <string_view>

namespace gtracker {
// SHA-256 over the supplied bytes, used for original-source identity.
std::string sha256(std::string_view bytes);
}
