#include "vfs/NodeStore.hpp"

#include "db/Statement.hpp"
#include "db/Transaction.hpp"
#include "util/Error.hpp"

#include <cerrno>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <chrono>
#include <format>

namespace virtualvaultfs::vfs {

namespace {

constexpr std::int64_t rootId = 1;

std::int64_t nowNs()
{
    return static_cast<std::int64_t>(std::time(nullptr)) * 1'000'000'000LL;
}

std::int64_t timespecToNs(const timespec& value)
{
    return static_cast<std::int64_t>(value.tv_sec) * 1'000'000'000LL + value.tv_nsec;
}

Node nodeFromStatement(const db::Statement& statement)
{
    Node node;
    node.id = statement.columnInt64(0);
    node.parentId = statement.columnInt64(1);
    node.name = statement.columnText(2);
    node.type = nodeTypeFromString(statement.columnText(3));
    node.objectId = statement.columnOptionalInt64(4);
    node.mode = statement.columnInt64(5);
    node.uid = statement.columnInt64(6);
    node.gid = statement.columnInt64(7);
    node.size = statement.columnInt64(8);
    node.mtimeNs = statement.columnInt64(9);
    node.ctimeNs = statement.columnInt64(10);
    node.objectRelpath = statement.columnText(11);
    return node;
}

const char* selectNodeSql()
{
    return "SELECT n.id, n.parent_id, n.name, n.type, n.object_id, n.mode, n.uid, n.gid, "
           "n.size, n.mtime_ns, n.ctime_ns, COALESCE(o.relpath, '') "
           "FROM nodes n LEFT JOIN objects o ON o.id = n.object_id ";
}

struct RootAttributes {
    std::int64_t mode{};
    std::int64_t uid{};
    std::int64_t gid{};
    std::int64_t size{};
    std::int64_t mtimeNs{};
    std::int64_t ctimeNs{};
};

RootAttributes rootAttributesForDatabase(const db::Database& database)
{
    if (database.path() == ":memory:") {
        const auto now = nowNs();
        return RootAttributes{
            .mode = 0755,
            .uid = static_cast<std::int64_t>(getuid()),
            .gid = static_cast<std::int64_t>(getgid()),
            .size = 0,
            .mtimeNs = now,
            .ctimeNs = now,
        };
    }

    auto vaultRoot = database.path().parent_path().parent_path();
    if (vaultRoot.empty()) {
        vaultRoot = ".";
    }

    struct stat physical {};
    if (::stat(vaultRoot.c_str(), &physical) != 0) {
        throw util::Error("failed to stat vault root '" + vaultRoot.string() + "': " + std::strerror(errno));
    }

    return RootAttributes{
        .mode = static_cast<std::int64_t>(physical.st_mode & 07777),
        .uid = static_cast<std::int64_t>(physical.st_uid),
        .gid = static_cast<std::int64_t>(physical.st_gid),
        .size = static_cast<std::int64_t>(physical.st_size),
        .mtimeNs = timespecToNs(physical.st_mtim),
        .ctimeNs = timespecToNs(physical.st_ctim),
    };
}

} // namespace

NodeStore::NodeStore()
    : ownedDatabase_(std::make_unique<db::Database>(":memory:"))
    , database_(ownedDatabase_.get())
{
    initializeSchema();
    ensureRoot();
}

NodeStore::NodeStore(db::Database& database)
    : database_(&database)
{
    initializeSchema();
    ensureRoot();
}

Node NodeStore::root() const
{
    auto node = find(rootId);
    if (!node.has_value()) {
        throw util::Error("root node is missing");
    }
    return *node;
}

Node NodeStore::add(Node node)
{
    db::Statement statement{
        database(),
        "INSERT INTO nodes(parent_id, name, type, object_id, mode, uid, gid, size, mtime_ns, ctime_ns) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"};
    statement.bind(1, node.parentId);
    statement.bind(2, node.name);
    statement.bind(3, toString(node.type));
    if (node.objectId.has_value()) {
        statement.bind(4, *node.objectId);
    } else {
        statement.bindNull(4);
    }
    statement.bind(5, node.mode);
    statement.bind(6, node.uid);
    statement.bind(7, node.gid);
    statement.bind(8, node.size);
    statement.bind(9, node.mtimeNs);
    statement.bind(10, node.ctimeNs);
    statement.exec();
    node.id = database().lastInsertRowId();
    return node;
}

void NodeStore::updateNode(const Node& node)
{
    db::Statement statement{
        database(),
        "UPDATE nodes SET parent_id=?, name=?, type=?, object_id=?, mode=?, uid=?, gid=?, "
        "size=?, mtime_ns=?, ctime_ns=? WHERE id=?"};
    statement.bind(1, node.parentId);
    statement.bind(2, node.name);
    statement.bind(3, toString(node.type));
    if (node.objectId.has_value()) {
        statement.bind(4, *node.objectId);
    } else {
        statement.bindNull(4);
    }
    statement.bind(5, node.mode);
    statement.bind(6, node.uid);
    statement.bind(7, node.gid);
    statement.bind(8, node.size);
    statement.bind(9, node.mtimeNs);
    statement.bind(10, node.ctimeNs);
    statement.bind(11, node.id);
    statement.exec();
}

std::optional<Node> NodeStore::find(std::int64_t id) const
{
    db::Statement statement{database(), std::string{selectNodeSql()} + "WHERE n.id=?"};
    statement.bind(1, id);
    if (!statement.step()) {
        return std::nullopt;
    }
    return nodeFromStatement(statement);
}

std::optional<Node> NodeStore::lookup(std::int64_t parentId, const std::string& name) const
{
    db::Statement statement{database(), std::string{selectNodeSql()} + "WHERE n.parent_id=? AND n.name=?"};
    statement.bind(1, parentId);
    statement.bind(2, name);
    if (!statement.step()) {
        return std::nullopt;
    }
    return nodeFromStatement(statement);
}

std::vector<Node> NodeStore::readdir(std::int64_t directoryId) const
{
    db::Statement statement{database(), std::string{selectNodeSql()} + "WHERE n.parent_id=? ORDER BY n.name"};
    statement.bind(1, directoryId);

    std::vector<Node> nodes;
    while (statement.step()) {
        nodes.push_back(nodeFromStatement(statement));
    }
    return nodes;
}

Node NodeStore::mkdir(std::int64_t parentId, const std::string& name, std::int64_t mode, std::int64_t uid, std::int64_t gid)
{
    Node node;
    node.parentId = parentId;
    node.name = name;
    node.type = NodeType::Directory;
    node.mode = mode;
    node.uid = uid;
    node.gid = gid;
    node.mtimeNs = nowNs();
    node.ctimeNs = node.mtimeNs;
    return add(node);
}

void NodeStore::rename(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName)
{
    db::Statement statement{database(), "UPDATE nodes SET parent_id=?, name=?, ctime_ns=? WHERE id=?"};
    statement.bind(1, newParentId);
    statement.bind(2, newName);
    statement.bind(3, nowNs());
    statement.bind(4, nodeId);
    statement.exec();
}

std::optional<Node> NodeStore::renameReplace(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName)
{
    db::Transaction transaction{database()};

    const auto source = find(nodeId);
    if (!source.has_value()) {
        return std::nullopt;
    }

    const auto replaced = lookup(newParentId, newName);
    if (replaced.has_value() && replaced->id != nodeId) {
        db::Statement deleteNode{database(), "DELETE FROM nodes WHERE id=?"};
        deleteNode.bind(1, replaced->id);
        deleteNode.exec();

        if (replaced->objectId.has_value()) {
            db::Statement deleteObject{database(), "DELETE FROM objects WHERE id=?"};
            deleteObject.bind(1, *replaced->objectId);
            deleteObject.exec();
        }
    }

    db::Statement renameNode{database(), "UPDATE nodes SET parent_id=?, name=?, ctime_ns=? WHERE id=?"};
    renameNode.bind(1, newParentId);
    renameNode.bind(2, newName);
    renameNode.bind(3, nowNs());
    renameNode.bind(4, nodeId);
    renameNode.exec();

    transaction.commit();

    if (replaced.has_value() && replaced->id != nodeId) {
        return replaced;
    }
    return std::nullopt;
}

std::optional<std::int64_t> NodeStore::unlink(std::int64_t nodeId)
{
    const auto node = find(nodeId);
    if (!node.has_value()) {
        return std::nullopt;
    }

    db::Transaction transaction{database()};
    db::Statement statement{database(), "DELETE FROM nodes WHERE id=?"};
    statement.bind(1, nodeId);
    statement.exec();
    transaction.commit();
    return node->objectId;
}

std::int64_t NodeStore::createObject()
{
    auto now = std::chrono::system_clock::now();
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();

    // Try reserve objet id with fake temp relpath because relpath must be unique
    std::string rel_path = std::format("{}", now_ns);
    db::Statement statement{database(), "INSERT INTO objects (relpath, size) VALUES (?, ?)"};
    statement.bind(1, rel_path);
    statement.bind(2, 0);
    statement.exec();

    return database().lastInsertRowId();
}

void NodeStore::removeObject(std::int64_t objectId)
{
    db::Statement statement{database(), "DELETE FROM objects WHERE id=?"};
    statement.bind(1, objectId);
    statement.exec();
}

void NodeStore::updateObject(std::int64_t objectId, const std::string& relpath, std::size_t size, bool update_hash_hint)
{
    (void)update_hash_hint;

    db::Statement statement{database(), "UPDATE objects SET relpath=?, size=? WHERE id=?"};
    statement.bind(1, relpath);
    statement.bind(2, static_cast<std::int64_t>(size));
    statement.bind(3, objectId);
    statement.exec();
}

void NodeStore::initializeSchema()
{
    database().exec(
        "CREATE TABLE IF NOT EXISTS nodes ("
        "id INTEGER PRIMARY KEY,"
        "parent_id INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "type TEXT NOT NULL,"
        "object_id INTEGER,"
        "mode INTEGER,"
        "uid INTEGER,"
        "gid INTEGER,"
        "size INTEGER,"
        "mtime_ns INTEGER,"
        "ctime_ns INTEGER,"
        "UNIQUE(parent_id, name));"
        "CREATE TABLE IF NOT EXISTS objects ("
        "id INTEGER PRIMARY KEY,"
        "relpath TEXT NOT NULL UNIQUE,"
        "size INTEGER,"
        "hash TEXT);"
        "CREATE INDEX IF NOT EXISTS idx_nodes_parent_name ON nodes(parent_id, name);");
}

void NodeStore::ensureRoot()
{
    if (find(rootId).has_value()) {
        return;
    }

    const auto attrs = rootAttributesForDatabase(database());

    db::Statement statement{
        database(),
        "INSERT INTO nodes(id, parent_id, name, type, object_id, mode, uid, gid, size, mtime_ns, ctime_ns) "
        "VALUES(?, ?, ?, ?, NULL, ?, ?, ?, ?, ?, ?)"
    };

    statement.bind(1, 1);
    statement.bind(2, 0);
    statement.bind(3, "/");
    statement.bind(4, "dir");
    statement.bind(5, attrs.mode);
    statement.bind(6, attrs.uid);
    statement.bind(7, attrs.gid);
    statement.bind(8, attrs.size);
    statement.bind(9, attrs.mtimeNs);
    statement.bind(10, attrs.ctimeNs);
    statement.exec();
}

db::Database& NodeStore::database() const
{
    return *database_;
}

std::string toString(NodeType type)
{
    switch (type) {
    case NodeType::File:
        return "file";
    case NodeType::Directory:
        return "dir";
    case NodeType::Symlink:
        return "symlink";
    }
    return "file";
}

NodeType nodeTypeFromString(const std::string& value)
{
    if (value == "dir") {
        return NodeType::Directory;
    }
    if (value == "symlink") {
        return NodeType::Symlink;
    }
    return NodeType::File;
}

} // namespace virtualvaultfs::vfs
