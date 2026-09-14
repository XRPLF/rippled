#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace xrpl {

/**
 * A file written for its owner only and replaced whole.
 *
 * The content goes to a temporary beside the target, restricted to the owner
 * before the first byte, and the target is replaced in one step by `commit`.
 * Without `commit` the temporary is removed, so a failed command leaves the
 * previous target as it was.
 */
class OwnerOnlyFile
{
    std::filesystem::path target_;
    std::filesystem::path temp_;
    // Names the file in errors: "key file", "output file".
    std::string what_;
    std::ofstream stream_;
    bool committed_ = false;

public:
    /**
     * Opens the temporary.
     *
     * @throws std::runtime_error if the target or the temporary is a symlink,
     *         or the temporary cannot be opened or restricted
     */
    OwnerOnlyFile(std::filesystem::path target, std::string what);
    ~OwnerOnlyFile();

    OwnerOnlyFile(OwnerOnlyFile const&) = delete;
    OwnerOnlyFile&
    operator=(OwnerOnlyFile const&) = delete;

    void
    write(std::string const& text);

    /**
     * Replaces the target with what was written.
     *
     * @throws std::runtime_error if the content could not be written or the
     *         target could not be replaced
     */
    void
    commit();

    [[nodiscard]] std::filesystem::path const&
    target() const
    {
        return target_;
    }
};

}  // namespace xrpl
