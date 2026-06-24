#pragma once

#include <xrpl/beast/unit_test.h>

#include <boost/filesystem.hpp>

#include <ostream>
#include <stdexcept>
#include <string>

namespace xrpl {

/**
   Write a key file dir and remove when done.
 */
class KeyFileGuard
{
private:
    using Path = boost::filesystem::path;

    beast::unit_test::Suite& test_;
    Path subDir_;

    void
    rmDir(Path const& toRm)
    {
        if (boost::filesystem::is_directory(toRm))
            boost::filesystem::remove_all(toRm);
        else
            test_.log << "Expected " << toRm.string() << " to be an existing directory."
                      << std::endl;
    }

public:
    KeyFileGuard(beast::unit_test::Suite& test, std::string const& subDir)
        : test_(test), subDir_(subDir)
    {
        if (!boost::filesystem::exists(subDir_))
            boost::filesystem::create_directory(subDir_);
        else
            // Cannot run the test. Someone created a file or directory
            // where we want to put our directory
            throw std::runtime_error("Cannot create directory: " + subDir_.string());
    }
    ~KeyFileGuard()
    {
        try
        {
            rmDir(subDir_);
        }
        catch (std::exception const& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~KeyFileGuard: " << e.what() << std::endl;
        }
    }
};

}  // namespace xrpl
