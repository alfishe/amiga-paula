#pragma once

#include <string>
#include <memory>
#include "types.hpp"

namespace mod {

std::unique_ptr<Module> loadMod(const std::string& filename);

} // namespace mod
