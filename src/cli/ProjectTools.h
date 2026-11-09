#pragma once
#include <filesystem>
#include <string>
namespace skrumi::cli {
struct ProjectManifest {
    std::string name, version;
    std::filesystem::path root, entry, resources;
    bool denyWarnings = false;
};
ProjectManifest manifest(const std::filesystem::path& directory);
int initProject(const std::string& directory);
const char* projectThirdPartyLicenses();
}
