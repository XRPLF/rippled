#include <tools/validator-keys/OwnerOnlyFile.h>

#include <sys/stat.h>

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl {

namespace fs = std::filesystem;

namespace {

// Syncs the directory holding @p file so the entry a rename or link made
// reaches the disk sooner. Best effort: the file's data is already synced,
// the entry is in place, and some filesystems refuse fsync on a directory.
// opendir gives the descriptor without a variadic open.
void
syncDirectory(fs::path const& file)
{
    auto dir = file.parent_path();
    if (dir.empty())
        dir = ".";
    DIR* const d = ::opendir(dir.c_str());
    if (d == nullptr)
        return;
    ::fsync(::dirfd(d));
    ::closedir(d);
}

// A filesystem without hard links (FAT, exFAT, SMB and some FUSE mounts)
// refuses link with one of these. ENOTSUP and EOPNOTSUPP are one value on
// Linux and two on macOS, so they are listed rather than compared.
constexpr std::array kNoHardLinkErrors{EPERM, ENOTSUP, EOPNOTSUPP};

bool
noHardLinks(int error)
{
    return std::ranges::find(kNoHardLinkErrors, error) != kNoHardLinkErrors.end();
}

}  // namespace

OwnerOnlyFile::OwnerOnlyFile(fs::path target, std::string what, Existing existing, int mode)
    : target_(std::move(target)), what_(std::move(what)), existing_(existing), mode_(mode)
{
    if (fs::is_symlink(target_))
        throw std::runtime_error("Refusing to write through a symlink: " + target_.string());

    // mkstemp creates the file itself, exclusively and mode 0600, so there is
    // no separate check-then-open step for a symlink to race.
    std::string templ = target_.string() + ".XXXXXX";
    fd_ = ::mkstemp(templ.data());
    if (fd_ < 0)
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());
    temp_ = templ;
    // A published mode is set before the first byte, on the descriptor.
    if (mode_ != kOwnerOnly && ::fchmod(fd_, mode_) != 0)
    {
        ::close(fd_);
        fd_ = -1;
        std::error_code ec;
        fs::remove(temp_, ec);
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());
    }
}

OwnerOnlyFile::~OwnerOnlyFile()
{
    if (!committed_)
    {
        if (fd_ >= 0)
            ::close(fd_);
        std::error_code ec;
        fs::remove(temp_, ec);
    }
}

void
OwnerOnlyFile::write(std::string const& text)
{
    std::size_t offset = 0;
    while (offset < text.size())
    {
        auto const n = ::write(fd_, text.data() + offset, text.size() - offset);
        if (n <= 0)
        {
            failed_ = true;
            return;
        }
        offset += static_cast<std::size_t>(n);
    }
}

void
OwnerOnlyFile::commit()
{
    // Synced before the rename: a file renamed into place with its data still
    // in the page cache can survive a crash empty.
    bool written = !failed_ && ::fsync(fd_) == 0;
    written = ::close(fd_) == 0 && written;
    fd_ = -1;
    bool exists = false;
    bool linked = false;
    bool unlinkable = false;
    if (written && existing_ == Existing::Refuse)
    {
        // link fails with EEXIST instead of replacing, so a concurrent creator is never lost.
        linked = ::link(temp_.c_str(), target_.c_str()) == 0;
        if (!linked)
        {
            written = false;
            exists = errno == EEXIST;
            unlinkable = noHardLinks(errno);
        }
    }
    if (written && !linked)
    {
        std::error_code ec;
        fs::rename(temp_, target_, ec);
        written = !ec;
    }

    // A successful link leaves the temporary as a second name for the target.
    if (!written || linked)
    {
        std::error_code ec;
        fs::remove(temp_, ec);
    }
    if (exists)
    {
        throw std::runtime_error(
            "Refusing to overwrite existing " + what_ + ": " + target_.string());
    }
    // A filesystem without hard links cannot hold an owner-only file either.
    if (unlinkable)
    {
        throw std::runtime_error(
            "Cannot create " + what_ + " on a filesystem without hard links: " + target_.string() +
            "; create it on a local filesystem and copy it");
    }
    if (!written)
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());
    syncDirectory(target_);
    committed_ = true;
}

}  // namespace xrpl
