#include "gtracker/project.hpp"
#include <stdexcept>

namespace gtracker {
void apply_lua(Pattern&, const std::filesystem::path&) {
    throw std::runtime_error("Lua support is disabled in this build; configure with GT_ENABLE_LUA=ON");
}
}
