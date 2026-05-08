#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace virtualvaultfs::vfs {

enum class NodeType {
    File,
    Directory,
    Symlink,
};

struct Node {
    std::int64_t id{};
    std::int64_t parentId{};
    std::string name;
    NodeType type{NodeType::File};
    std::optional<std::int64_t> objectId;
    std::string objectRelpath;
    std::int64_t mode{};
    std::int64_t uid{};
    std::int64_t gid{};
    std::int64_t size{};
    std::int64_t mtimeNs{};
    std::int64_t ctimeNs{};
};

std::string toString(NodeType type);
NodeType nodeTypeFromString(const std::string& value);

} // namespace virtualvaultfs::vfs
