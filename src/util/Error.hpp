#pragma once

#include <stdexcept>
#include <string>

namespace virtualvaultfs::util {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message);
};

} // namespace virtualvaultfs::util
