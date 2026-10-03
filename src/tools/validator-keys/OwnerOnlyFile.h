#pragma once

#include <filesystem>
#include <string>

namespace xrpl {

/**
 * A file written for its owner only and replaced whole.
 *
 * The content goes to a temporary created beside the target under a name
 * the system chooses, exclusively and restricted to the owner from its
 * first byte, and the target is replaced in one step by `commit`. Without
 * `commit` the temporary is removed, so a failed command leaves the
 * previous target as it was.
 */
class OwnerOnlyFile
{
    std::filesystem::path target_;
    std::filesystem::path temp_;
    // Names the file in errors: "key file", "output file".
    std::string what_;
    int fd_ = -1;
    bool failed_ = false;
    bool committed_ = false;

public:
    /**
     * Creates the temporary, owner-only before the first byte is written.
     *
     * @throws std::runtime_error if the target is a symlink, or the
     *         temporary could not be created
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
