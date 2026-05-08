#include "storage/ObjectStore.hpp"

#include <cassert>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <vector>

void runNodeStoreTests();
void runPathResolverTests();

void runObjectStoreTests()
{
    virtualvaultfs::storage::ObjectStore store{"objects"};
    assert(store.pathFor("abc") == std::filesystem::path{"objects"} / "abc");

    const auto testRoot = std::filesystem::temp_directory_path() / "virtualvaultfs-objectstore-tests";
    std::filesystem::remove_all(testRoot);

    virtualvaultfs::storage::ObjectStore physicalStore{testRoot};
    auto handle = physicalStore.open("a/blob", O_CREAT | O_RDWR, 0644);

    const std::string content = "hello";
    const auto* begin = reinterpret_cast<const std::byte*>(content.data());
    const std::span data{begin, content.size()};
    assert(handle.write(data, 0) == content.size());

    const auto bytes = handle.read(content.size(), 0);
    assert(bytes.size() == content.size());
    const std::string actual{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    assert(actual == content);

    std::filesystem::remove_all(testRoot);
}

int main()
{
    runNodeStoreTests();
    runPathResolverTests();
    runObjectStoreTests();
    return 0;
}
