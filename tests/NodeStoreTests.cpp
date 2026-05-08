#include "vfs/NodeStore.hpp"

#include <cassert>

void runNodeStoreTests()
{
    virtualvaultfs::vfs::NodeStore store;
    assert(store.root().name == "/");

    const auto docs = store.mkdir(store.root().id, "docs", 0755, 1000, 1000);
    const auto foundDocs = store.lookup(store.root().id, "docs");
    assert(foundDocs.has_value());
    assert(foundDocs->id == docs.id);

    virtualvaultfs::vfs::Node node;
    node.parentId = docs.id;
    node.name = "file.txt";
    node.objectId = 42;
    const auto saved = store.add(node);

    const auto found = store.find(saved.id);
    assert(found.has_value());
    assert(found->name == "file.txt");

    const auto docsChildren = store.readdir(docs.id);
    assert(docsChildren.size() == 1);
    assert(docsChildren[0].name == "file.txt");

    store.rename(saved.id, store.root().id, "renamed.txt");
    assert(!store.lookup(docs.id, "file.txt").has_value());
    assert(store.lookup(store.root().id, "renamed.txt").has_value());

    const auto objectId = store.unlink(saved.id);
    assert(objectId.has_value());
    assert(*objectId == 42);
    store.removeObject(*objectId);
    assert(!store.find(saved.id).has_value());
}
