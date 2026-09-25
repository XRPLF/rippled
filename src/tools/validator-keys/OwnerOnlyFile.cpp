#include <tools/validator-keys/OwnerOnlyFile.h>

#include <filesystem>
#include <initializer_list>
#include <ios>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace xrpl {

namespace fs = std::filesystem;

OwnerOnlyFile::OwnerOnlyFile(fs::path target, std::string what)
    : target_(std::move(target)), temp_(target_.string() + ".tmp"), what_(std::move(what))
{
    for (auto const& path : {target_, temp_})
    {
        if (fs::is_symlink(path))
            throw std::runtime_error("Refusing to write through a symlink: " + path.string());
    }

    stream_.open(temp_, std::ios_base::trunc);
    if (stream_.fail())
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());

    std::error_code ec;
    fs::permissions(temp_, fs::perms::owner_read | fs::perms::owner_write, ec);
    if (ec)
    {
        stream_.close();        // LCOV_EXCL_LINE
        fs::remove(temp_, ec);  // LCOV_EXCL_LINE
        throw std::runtime_error(                                 // LCOV_EXCL_LINE
            "Cannot write " + what_ + ": " + target_.string());  // LCOV_EXCL_LINE
    }
}

OwnerOnlyFile::~OwnerOnlyFile()
{
    if (!committed_)
    {
        stream_.close();
        std::error_code ec;
        fs::remove(temp_, ec);
    }
}

void
OwnerOnlyFile::write(std::string const& text)
{
    stream_ << text;
}

void
OwnerOnlyFile::commit()
{
    stream_.close();
    std::error_code ec;
    if (!stream_.fail())
        fs::rename(temp_, target_, ec);
    if (stream_.fail() || ec)
    {
        fs::remove(temp_, ec);
        throw std::runtime_error("Cannot write " + what_ + ": " + target_.string());
    }
    committed_ = true;
}

}  // namespace xrpl
