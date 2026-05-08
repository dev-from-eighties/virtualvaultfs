#include "vfs/PathResolver.hpp"

#include <cassert>

void runPathResolverTests()
{
    const auto parts = virtualvaultfs::vfs::PathResolver::split("/alpha/beta");
    assert(parts.size() == 2);
    assert(parts[0] == "alpha");
    assert(parts[1] == "beta");
}
