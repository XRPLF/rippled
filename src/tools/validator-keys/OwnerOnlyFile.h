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
public:
    // Whether `commit` may replace a target that already exists.
    enum class Existing { Replace, Refuse };

private:
    std::filesystem::path target_;
    std::filesystem::path temp_;
    // Names the file in errors: "key file", "output file".
    std::string what_;
    Existing existing_;
    int fd_ = -1;
    bool failed_ = false;
    bool committed_ = false;

public:
    /**
     * Creates the temporary, owner-only before the first byte is written.
     *
     * @param existing `Refuse` makes `commit` fail when the target exists,
     *        checked in the same step that creates it
     *
     * @throws std::runtime_error if the target is a symlink, or the
     *         temporary could not be created
     */
    OwnerOnlyFile(
        std::filesystem::path target,
        std::string what,
        Existing existing = Existing::Replace);
    ~OwnerOnlyFile();

    OwnerOnlyFile(OwnerOnlyFile const&) = delete;
    OwnerOnlyFile&
    operator=(OwnerOnlyFile const&) = delete;

    void
    write(std::string const& text);

    /**
     * Replaces the target with what was written, or with `Existing::Refuse`
     * creates it.
     *
     * @throws std::runtime_error if the content could not be written, the
     *         target could not be replaced, or with `Existing::Refuse` the
     *         target exists
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
