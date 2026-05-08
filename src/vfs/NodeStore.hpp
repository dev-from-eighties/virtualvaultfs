#pragma once

#include "db/Database.hpp"
#include "vfs/Node.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace virtualvaultfs::vfs {

class NodeStore {
public:
    NodeStore();
    explicit NodeStore(db::Database& database);

    Node root() const;
    Node add(Node node);
    void updateNode(const Node& node);
    std::optional<Node> find(std::int64_t id) const;
    std::optional<Node> lookup(std::int64_t parentId, const std::string& name) const;
    std::vector<Node> readdir(std::int64_t directoryId) const;
    Node mkdir(std::int64_t parentId, const std::string& name, std::int64_t mode, std::int64_t uid, std::int64_t gid);
    void rename(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName);
    std::optional<Node> renameReplace(std::int64_t nodeId, std::int64_t newParentId, const std::string& newName);
    std::optional<std::int64_t> unlink(std::int64_t nodeId);

    /// @brief Create a new Object with unique id and fake unique relpath in database.
    ///
    /// At this point object still doesn't exist in filesystem. You must use vfs::ObjectStore
    /// to attemp creation of the file in the underlying filesystem and then update
    /// object's entry in database using NodeStore::updateObject()
    ///
    /// @return id of newly created object in database.
    std::int64_t createObject();

    /** Update an object row in database to inform size changes, set final relpath, and trigger hash recalculation.
     *
     * Note that hash may be not in use. This is specially true in first version
     * where field exists in database but was not used. In implementation supporting
     * hashing, disabling it may improve performance.
     *
     * @param objectId unique id of object as stored in id column in objects table in database.
     *
     * @param relpath unique string in objects table in database. Is easy identify temp relpaths, they do not have '.blob'. relpath should not have '/'.
     *
     * @param update_hash_hint even when false hash will be updated if size differs from databse.
     */
    void updateObject(std::int64_t objectId, const std::string& relpath, std::size_t size, bool update_hash_hint = false);

    void removeObject(std::int64_t objectId);

private:
    void initializeSchema();
    void ensureRoot();
    db::Database& database() const;

    std::unique_ptr<db::Database> ownedDatabase_;
    db::Database* database_{nullptr};
};

} // namespace virtualvaultfs::vfs
