#include "util/Error.hpp"

namespace virtualvaultfs::util {

Error::Error(const std::string& message)
    : std::runtime_error(message)
{
}

} // namespace virtualvaultfs::util
