#pragma once

#include <filesystem>
#include <string>

namespace xrpl {

/**
 * A file written whole and replaced in one step.
 *
 * The content goes to a temporary created beside the target under a name
 * the system chooses, exclusively and restricted to the owner from its
 * first byte, and the target is replaced in one step by `commit`, which
 * also syncs the content and the directory entry to disk. Without `commit`
 * the temporary is removed, so a failed command leaves the previous target
 * as it was. The target is readable by its owner only unless `kPublished`
 * is given, for a document that is meant to be served.
 */
class OwnerOnlyFile
{
public:
    // Whether `commit` may replace a target that already exists.
    enum class Existing { Replace, Refuse };

    // Modes of the target: readable by its owner only, or by everyone.
    static constexpr int kOwnerOnly = 0600;
    static constexpr int kPublished = 0644;

private:
    std::filesystem::path target_;
    std::filesystem::path temp_;
    // Names the file in errors: "key file", "output file".
    std::string what_;
    Existing existing_;
    int mode_;
    int fd_ = -1;
    bool failed_ = false;
    bool committed_ = false;

public:
    /**
     * Creates the temporary, owner-only before the first byte is written.
     *
     * @param existing `Refuse` makes `commit` fail when the target exists,
     *        checked in the same step that creates it
     * @param mode The target's mode, `kOwnerOnly` or `kPublished`
     *
     * @throws std::runtime_error if the target is a symlink, or the
     *         temporary could not be created
     */
    OwnerOnlyFile(
        std::filesystem::path target,
        std::string what,
        Existing existing = Existing::Replace,
        int mode = kOwnerOnly);
    ~OwnerOnlyFile();

    OwnerOnlyFile(OwnerOnlyFile const&) = delete;
    OwnerOnlyFile&
    operator=(OwnerOnlyFile const&) = delete;

    void
    write(std::string const& text);

    /**
     * Replaces the target with what was written, or with `Existing::Refuse`
     * creates it: a hard link to the temporary, which fails when the target
     * exists, or where the filesystem has no hard links an exclusive create
     * of the name followed by a rename of the temporary over it. A create
     * that fails leaves no target.
     *
     * @throws std::runtime_error if the content could not be written or
     *         synced, the target could not be replaced, or with
     *         `Existing::Refuse` the target exists
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
