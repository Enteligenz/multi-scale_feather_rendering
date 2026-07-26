#include <lightwave/core.hpp>
#include <filesystem>

namespace fs = std::filesystem;

namespace lightwave {

std::string relpath(const std::string &path) {
    try {
        return fs::relative(path, fs::current_path()).string();
    } catch (...) {
        return path;
    }
}

}
