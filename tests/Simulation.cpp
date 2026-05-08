#include "db/Database.hpp"
#include "db/Statement.hpp"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

const fs::path vaultDir{".test_vv"};
const fs::path mountDir{"test_vv"};
const fs::path databasePath = vaultDir / "vault-data" / "metadata.sqlite3";
const fs::path objectsDir = vaultDir / "vault-data" / "objects";

std::string shellQuote(const fs::path& path)
{
    std::string quoted{"'"};
    for (char ch : path.string()) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += '\'';
    return quoted;
}

void writeFile(const fs::path& path, std::string_view content)
{
    std::ofstream out{path, std::ios::binary};
    assert(out);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    assert(out);
}

std::string objectRelpathFor(const virtualvaultfs::db::Database& db, std::string_view name)
{
    virtualvaultfs::db::Statement statement{
        db,
        "SELECT o.relpath FROM nodes n JOIN objects o ON o.id = n.object_id WHERE n.name=?"};
    statement.bind(1, std::string{name});
    assert(statement.step());
    return statement.columnText(0);
}

std::string objectRelpathFor(const virtualvaultfs::db::Database& db, std::string_view name, std::string_view type)
{
    virtualvaultfs::db::Statement statement{
        db,
        "SELECT o.relpath FROM nodes n JOIN objects o ON o.id = n.object_id WHERE n.name=? AND n.type=?"};
    statement.bind(1, std::string{name});
    statement.bind(2, std::string{type});
    assert(statement.step());
    return statement.columnText(0);
}

void assertPhysicalObjectFor(const virtualvaultfs::db::Database& db, std::string_view name)
{
    assert(fs::is_regular_file(objectsDir / objectRelpathFor(db, name)));
}

void assertFileContent(const fs::path& path, std::string_view expected)
{
    std::ifstream in{path, std::ios::binary};
    assert(in);
    const std::string actual{
        std::istreambuf_iterator<char>{in},
        std::istreambuf_iterator<char>{}};
    assert(actual == expected);
}

std::int64_t nodeIdFor(const virtualvaultfs::db::Database& db, std::int64_t parentId, std::string_view name)
{
    virtualvaultfs::db::Statement statement{
        db,
        "SELECT id FROM nodes WHERE parent_id=? AND name=? AND type='dir'"};
    statement.bind(1, parentId);
    statement.bind(2, std::string{name});
    assert(statement.step());
    return statement.columnInt64(0);
}

bool isMounted()
{
    std::error_code error;
    const auto target = fs::weakly_canonical(mountDir, error);
    if (error) {
        return false;
    }

    std::ifstream mounts{"/proc/self/mountinfo"};
    std::string line;
    while (std::getline(mounts, line)) {
        if (line.find(" " + target.string() + " ") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void waitForMount()
{
    for (int i = 0; i < 100; ++i) {
        if (isMounted()) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    assert(false && "mount did not become usable");
}

} // namespace

int main(int argc, char** argv)
{
    std::error_code error;
    const fs::path self = fs::weakly_canonical(argv[0], error);
    assert(!error);
    const fs::path binary = fs::weakly_canonical(
        argc > 1 ? fs::path{argv[1]} : self.parent_path().parent_path() / "virtualvaultfs",
        error);
    assert(!error);

    fs::current_path(self.parent_path());

    std::system(("fusermount3 -uz " + shellQuote(mountDir) + " >/dev/null 2>&1").c_str());
    fs::remove_all(vaultDir);
    fs::remove_all(mountDir);
    fs::create_directory(vaultDir);
    fs::create_directory(mountDir);

    const std::string initCommand = shellQuote(binary) + " initialize " + shellQuote(vaultDir);
    const int initResult = std::system(initCommand.c_str());
    assert(initResult != -1);
    assert(WIFEXITED(initResult));
    assert(WEXITSTATUS(initResult) == 0);

    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(binary.c_str(), binary.c_str(), "mount", vaultDir.c_str(), mountDir.c_str(), nullptr);
        _exit(127);
    }

    waitForMount();

    const fs::path file1 = mountDir / "human-meaningful-file.txt";
    writeFile(file1, "bytes written through the mounted vault\n");

    virtualvaultfs::db::Database db{databasePath};
    assertPhysicalObjectFor(db, "human-meaningful-file.txt");

    const fs::path outsideFile{"outside-created-file.txt"};
    writeFile(outsideFile, "bytes written outside the mounted vault\n");
    const std::string moveCommand = "mv " + shellQuote(outsideFile) + " " + shellQuote(mountDir / "imported-from-outside.txt");
    const int moveResult = std::system(moveCommand.c_str());
    assert(moveResult != -1);
    assert(WIFEXITED(moveResult));
    assert(WEXITSTATUS(moveResult) == 0);
    assertPhysicalObjectFor(db, "imported-from-outside.txt");

    const fs::path absoluteMountDir = fs::weakly_canonical(mountDir);
    const fs::path symlinkPath = mountDir / "link-to-imported.txt";
    fs::create_symlink(absoluteMountDir / "imported-from-outside.txt", symlinkPath);

    const auto symlinkRelpath = objectRelpathFor(db, "link-to-imported.txt", "symlink");
    assert(fs::is_symlink(objectsDir / symlinkRelpath));

    constexpr std::string_view symlinkWriteContent{"bytes written through the symlink\n"};
    writeFile(symlinkPath, symlinkWriteContent);
    assertFileContent(objectsDir / objectRelpathFor(db, "imported-from-outside.txt", "file"), symlinkWriteContent);

    fs::create_directory(mountDir / "first-level");
    fs::create_directory(mountDir / "first-level" / "second-level");
    fs::rename(file1, mountDir / "first-level" / "second-level" / "human-meaningful-file.txt");

    kill(child, SIGTERM);
    int status = 0;
    waitpid(child, &status, 0);

    const std::int64_t rootId = 1;
    const std::int64_t firstLevelId = nodeIdFor(db, rootId, "first-level");
    nodeIdFor(db, firstLevelId, "second-level");

    fs::remove_all(vaultDir);
    fs::remove_all(mountDir);
    return 0;
}
