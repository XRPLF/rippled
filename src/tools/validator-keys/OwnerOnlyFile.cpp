#include <tools/validator-keys/OwnerOnlyFile.h>

#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl {

namespace fs = std::filesystem;

OwnerOnlyFile::OwnerOnlyFile(fs::path target, std::string what)
    : target_(std::move(target)), what_(std::move(what))
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
    auto const closeRc = ::close(fd_);
    fd_ = -1;
    std::error_code ec;
    if (!failed_ && closeRc == 0)
        fs::rename(temp_, target_, ec);
    if (failed_ || closeRc != 0 || ec)
    {
        fs::remove(temp_, ec);
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());
    }
    committed_ = true;
}

}  // namespace xrpl
