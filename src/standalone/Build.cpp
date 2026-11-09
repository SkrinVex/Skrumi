#include "Image.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace skrumi::bundle {
namespace fs = std::filesystem;
namespace {
class TemporaryDirectory {
public:
    fs::path path;
    explicit TemporaryDirectory(const fs::path& parent) {
        std::random_device random;
        for (int i = 0; i < 64; ++i) {
            auto candidate = parent / (".skmbuild-" + std::to_string(random()) + "-" + std::to_string(random()));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                path = std::move(candidate);
                return;
            }
            if (ec && ec != std::errc::file_exists) throw fs::filesystem_error("cannot create build directory", candidate, ec);
        }
        throw std::runtime_error("Build Error: cannot create unique temporary directory");
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
}

void build(const fs::path& input, fs::path output, bool noConsole, const fs::path& resourceDirectory, bool force) {
    if (output.empty()) output = input.stem();
    if (isSourceFile(platform::pathToUtf8(output)))
        throw std::runtime_error("Build Error: output must not overwrite source code");
#ifdef _WIN32
    auto extension = output.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".exe") output += ".exe";
#endif
    output = fs::absolute(output);
    const auto status = fs::symlink_status(output);
    if (fs::exists(status) && !force) throw std::runtime_error("Build Error: output already exists: " + platform::pathToUtf8(output) + "; use --force to replace it");
    if (fs::is_symlink(status) || (fs::exists(status) && !fs::is_regular_file(status)))
        throw std::runtime_error("Build Error: output must be a regular file, not a link or directory");
    if (isSourceFile(platform::pathToUtf8(output)) || fs::weakly_canonical(input) == fs::weakly_canonical(output) ||
        (fs::exists(status) && fs::equivalent(input, output)))
        throw std::runtime_error("Build Error: output must not overwrite source code");
    if (!fs::is_regular_file(input)) throw std::runtime_error("Build Error: could not open file '" + platform::pathToUtf8(input) + "'");
    auto sources = collect(platform::pathToUtf8(input), *filesystemSources());
    if (!resourceDirectory.empty()) {
        auto root = fs::absolute(resourceDirectory);
        auto rejectLink = [](const fs::path& path) {
            if (fs::is_symlink(fs::symlink_status(path)))
                throw std::runtime_error("Build Error: resource links are not allowed: " + platform::pathToUtf8(path));
#ifdef _WIN32
            auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Build Error: invalid resource or reparse point: " + platform::pathToUtf8(path));
#endif
        };
        rejectLink(root);
        if (!fs::is_directory(root)) throw std::runtime_error("Build Error: resources must be a directory");
        size_t total = encode(sources).size() + 4; // v2 resource count
        for (const auto& item : fs::recursive_directory_iterator(root)) {
            rejectLink(item.path());
            if (item.is_directory()) continue;
            if (!item.is_regular_file()) throw std::runtime_error("Build Error: resource must be a regular file");
            auto name = item.path().lexically_relative(root).generic_u8string();
            if (!validResourceName(name)) throw std::runtime_error("Build Error: invalid resource name: " + name);
            auto size = item.file_size();
            if (sources.resources.size() >= maxFiles || total > maxPayload ||
                size > maxPayload - total || name.size() + 8 > maxPayload - total - size)
                throw std::runtime_error("Build Error: resources exceed bundle limits");
            std::string data(static_cast<size_t>(size), '\0');
            std::ifstream stream(item.path(), std::ios::binary);
            if (!stream) throw std::runtime_error("Build Error: cannot read resource: " + name);
            stream.read(data.data(), static_cast<std::streamsize>(size));
            if (stream.gcount() != static_cast<std::streamsize>(size) || stream.bad() || stream.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("Build Error: resource read failed or size changed: " + name);
            total += static_cast<size_t>(size) + name.size() + 8;
            sources.resources.emplace(name, std::move(data));
        }
    }
    auto image = pack(readImage(executablePath()), sources, noConsole);
    TemporaryDirectory temporary(output.parent_path());
    auto candidate = temporary.path / "program";
    {
        std::ofstream stream(candidate, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        stream.close();
        if (!stream) throw std::runtime_error("Build Error: cannot write executable");
    }
    // Validate the bytes actually written before publishing the executable.
    unpack(readImage(candidate));
#ifdef _WIN32
    // Without --force the atomic move refuses an existing destination.
    if (!MoveFileExW(candidate.c_str(), output.c_str(), force ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH : 0)) throw std::runtime_error("Build Error: cannot publish output (Windows error " + std::to_string(GetLastError()) + ")");
#else
    fs::permissions(candidate, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec);
    // Same filesystem; link publishes atomically and never overwrites a source or output.
    if (force) fs::rename(candidate, output);
    else fs::create_hard_link(candidate, output);
#endif
}
} // namespace skrumi::bundle
