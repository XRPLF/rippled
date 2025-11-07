#ifndef TEST_UNIT_TEST_DIRGUARD_H
#define TEST_UNIT_TEST_DIRGUARD_H

#include <test/jtx/TestSuite.h>

#include <xrpl/basics/contract.h>

#include <boost/filesystem.hpp>

#include <fstream>

namespace ripple {
namespace detail {

/**
    Create a directory and remove it when it's done
*/
class DirGuard
{
protected:
    using path = boost::filesystem::path;

private:
    path subDir_;
    bool rmSubDir_{false};

protected:
    beast::unit_test::suite& test_;

    auto
    rmDir(path const& toRm)
    {
        if (is_directory(toRm) && is_empty(toRm))
            remove(toRm);
        else
            test_.log << "Expected " << toRm.string()
                      << " to be an empty existing directory." << std::endl;
    }

public:
    DirGuard(beast::unit_test::suite& test, path subDir, bool useCounter = true)
        : subDir_(std::move(subDir)), test_(test)
    {
        using namespace boost::filesystem;

        static auto subDirCounter = 0;
        if (useCounter)
            subDir_ += std::to_string(++subDirCounter);
        if (!exists(subDir_))
        {
            create_directory(subDir_);
            rmSubDir_ = true;
        }
        else if (is_directory(subDir_))
            rmSubDir_ = false;
        else
        {
            // Cannot run the test. Someone created a file where we want to
            // put our directory
            Throw<std::runtime_error>(
                "Cannot create directory: " + subDir_.string());
        }
    }

    ~DirGuard()
    {
        try
        {
            using namespace boost::filesystem;

            if (rmSubDir_)
                rmDir(subDir_);
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~DirGuard: " << e.what() << std::endl;
        };
    }

    path const&
    subdir() const
    {
        return subDir_;
    }
};

/**
    Write a file in a directory and remove when done
*/
class FileDirGuard : public DirGuard
{
protected:
    path const file_;
    bool created_ = false;

public:
    FileDirGuard(
        beast::unit_test::suite& test,
        path subDir,
        path file,
        std::string const& contents,
        bool useCounter = true,
        bool create = true)
        : DirGuard(test, subDir, useCounter)
        , file_(file.is_absolute() ? file : subdir() / file)
    {
        if (!exists(file_))
        {
            if (create)
            {
                std::ofstream o(file_.string());
                o << contents;
                created_ = true;
            }
        }
        else
        {
            Throw<std::runtime_error>(
                "Refusing to overwrite existing file: " + file_.string());
        }
    }

    ~FileDirGuard()
    {
        try
        {
            using namespace boost::filesystem;
            if (exists(file_))
            {
                remove(file_);
            }
            else
            {
                if (created_)
                    test_.log << "Expected " << file_.string()
                              << " to be an existing file." << std::endl;
            }
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~FileGuard: " << e.what() << std::endl;
        };
    }

    path const&
    file() const
    {
        return file_;
    }

    bool
    fileExists() const
    {
        return boost::filesystem::exists(file_);
    }
};

}  // namespace detail
}  // namespace ripple

#endif  // TEST_UNIT_TEST_DIRGUARD_H
