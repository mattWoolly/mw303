#pragma once
// The factory content embedded in the binary: every assets/factory/**/*.mw12f, by its path
// relative to assets/factory ("scene/Rubber Floor.mw12f", "default.mw12f"). The table is
// generated at build time (plugin/src/embed/EmbedFiles.cpp, see plugin/CMakeLists.txt) and
// rebuilt whenever a file changes; names may repeat across types because the path is the key.
#include <cstddef>
#include <span>
#include <string_view>

namespace acid::plugin
{
struct FactoryFile
{
    const char* path;             // relative to assets/factory, '/' separated
    const unsigned char* data;    // the file's bytes (not NUL terminated)
    std::size_t size;

    std::string_view text() const noexcept { return { reinterpret_cast<const char*> (data), size }; }
};

/** Every embedded file, in path order. Empty when the factory directory was empty at build time. */
std::span<const FactoryFile> factoryFiles() noexcept;

/** The file at that relative path, or nullptr. */
const FactoryFile* findFactoryFile (std::string_view path) noexcept;
}
