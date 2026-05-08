#define FUSE_USE_VERSION 31

#include "app/App.hpp"

#include "Config.hpp"
#include "fuse/FuseMain.hpp"
#include "storage/BackendPaths.hpp"
#include "util/Logger.hpp"

#include <format>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/mount.h>
#include <thread>
#include <utility>
#include <vector>

/*
Procedure: first run initialize, then mount.

initialize is mean to be used by human when setting up directories.

mount can be automatized at system startup, but it won't auto initialize anything.

This way mount is protected from typo errors.
*/

namespace virtualvaultfs::app {

Config* config = nullptr;

namespace {

std::string shellQuote(const std::filesystem::path& path)
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

class MountSignalHandler {
public:
    explicit MountSignalHandler(std::filesystem::path mountPoint)
        : mountPoint_(std::move(mountPoint))
    {
        sigemptyset(&signals_);
        sigaddset(&signals_, SIGINT);
        sigaddset(&signals_, SIGTERM);
        sigaddset(&signals_, SIGHUP);
        sigaddset(&signals_, SIGUSR1);

        if (pthread_sigmask(SIG_BLOCK, &signals_, &previousMask_) != 0) {
            util::Logger::error("ERROR: mount: could not install signal mask");
            return;
        }

        active_ = true;
        thread_ = std::thread{[this] { waitForSignals(); }};
    }

    ~MountSignalHandler()
    {
        if (thread_.joinable()) {
            pthread_kill(thread_.native_handle(), SIGUSR1);
            thread_.join();
        }

        if (active_) {
            pthread_sigmask(SIG_SETMASK, &previousMask_, nullptr);
        }
    }

    MountSignalHandler(const MountSignalHandler&) = delete;
    MountSignalHandler& operator=(const MountSignalHandler&) = delete;

private:
    void waitForSignals()
    {
        for (;;) {
            int signal = 0;
            if (sigwait(&signals_, &signal) != 0) {
                return;
            }

            if (signal == SIGUSR1) {
                return;
            }

            util::Logger::info(std::format("mount: received signal {}, unmounting {}", signal, mountPoint_.string()));
            requestUnmount();
            return;
        }
    }

    void requestUnmount() const
    {
        if (::umount2(mountPoint_.c_str(), MNT_DETACH) == 0) {
            return;
        }

        const std::string command = "fusermount3 -uz " + shellQuote(mountPoint_) + " >/dev/null 2>&1";
        (void)std::system(command.c_str());
    }

    std::filesystem::path mountPoint_;
    sigset_t signals_{};
    sigset_t previousMask_{};
    std::thread thread_;
    bool active_{false};
};

} // namespace

bool is_path_initialized (std::filesystem::path& path);

bool cmd_initialize_dir (std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        util::Logger::error(std::format("ERROR: initialize: Creating directory with code = {}: target path is not a directory.", error.value()));
        return false;
    }

    if (is_path_initialized(path))
    {
        util::Logger::error(std::format("ERROR: initialize: Target directory was already initialized."));
        return false;
    }

    const auto paths = storage::makeBackendPaths(path / Config{}.storageRoot);

    error.clear();
    if (!std::filesystem::create_directory(paths.root))
    {
        util::Logger::error(std::format("ERROR: initialize: Could not create {}, error code = {}", paths.root.string(), error.value()));
        return false;
    }

    error.clear();
    if (!std::filesystem::create_directory(paths.objects))
    {
        util::Logger::error(std::format("ERROR: initialize: Could not create {}, error code = {}", paths.objects.string(), error.value()));
        return false;
    }

    std::ofstream metadata{paths.metadata};
    if (!metadata)
    {
        util::Logger::error(std::format("ERROR: initialize: Could not create {}, error code = {}", paths.metadata.string(), error.value()));
        return false;
    }

    return is_path_initialized(path);
}

bool is_path_initialized (std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return false;
    }

    const auto paths = storage::makeBackendPaths(path / Config{}.storageRoot);

    error.clear();
    if (!std::filesystem::is_directory(paths.root, error)) {
        return false;
    }

    error.clear();
    if (!std::filesystem::is_directory(paths.objects, error)) {
        return false;
    }

    error.clear();
    return std::filesystem::is_regular_file(paths.metadata, error);
}

int App::run(int argc, char** argv)
{
    config = new Config();

    std::vector<std::string_view> pargs;
    pargs.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
    {
        pargs.push_back(argv[i]);
    }

    if (pargs.size() < 1)
    {
        util::Logger::info("Use initialize or mount to see all available commands.");
        return 1;
    } else
    if (pargs[0] == "initialize")
    {
        if (pargs.size() != 2)
        {
            util::Logger::error("initialize requires exactly 1 argument: path to target directory");
            return 1;
        }

        std::filesystem::path path{std::string{pargs[1]}};
        bool result = cmd_initialize_dir(path);
        if (!result)
        {
            util::Logger::error("initialize failed. Consider using 'clean' to remove incomplete directory structure");
            return 1;
        }

        util::Logger::info("OK");
        return 0;
    } else
    if (pargs[0] == "mount")
    {
        const bool allowOther = pargs.size() == 5 && pargs[3] == "-o" && pargs[4] == "allow_other";
        if (pargs.size() != 3 && !allowOther)
        {
            util::Logger::error("mount requires 2 arguments: path to previously initialized vault directory and path to target mount point");
            return 1;
        }

        std::filesystem::path vaultPath{std::string{pargs[1]}};
        std::filesystem::path mountPoint{std::string{pargs[2]}};
        const auto paths = storage::makeBackendPaths(vaultPath / config->storageRoot);

        config->mountPoint = mountPoint;
        config->databasePath = paths.metadata;

        std::string programName{argv[0] != nullptr ? argv[0] : "virtualvaultfs"};
        std::string foreground{"-f"};
        std::string singleThreaded{"-s"};
        std::string allow_other_o{"-o"};
        std::string allow_other{"allow_other"};
        std::string mountArg = mountPoint.string();
        std::vector<char*> fuseArgs{
            programName.data(),
            foreground.data(),
            singleThreaded.data(),
            mountArg.data()
        };
        if (allowOther)
        {
            fuseArgs.push_back(allow_other_o.data());
            fuseArgs.push_back(allow_other.data());
        }

        MountSignalHandler signalHandler{mountPoint};
        return fuse::runFuse(static_cast<int>(fuseArgs.size()), fuseArgs.data());
    }

    util::Logger::error(std::format("Unknown command: {}", pargs[0]));
    return 1;
}

} // namespace virtualvaultfs::app
