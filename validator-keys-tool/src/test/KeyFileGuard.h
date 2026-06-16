#include <xrpl/beast/unit_test.h>

#include <boost/filesystem.hpp>

#include <fstream>

namespace xrpl {

/**
   Write a key file dir and remove when done.
 */
class KeyFileGuard
{
private:
    using path = boost::filesystem::path;
    path subDir_;
    beast::unit_test::Suite& test_;

    auto
    rmDir(path const& toRm)
    {
        if (is_directory(toRm))
            remove_all(toRm);
        else
            test_.log << "Expected " << toRm.string() << " to be an existing directory."
                      << std::endl;
    };

public:
    KeyFileGuard(beast::unit_test::Suite& test, std::string const& subDir)
        : subDir_(subDir), test_(test)
    {
        using namespace boost::filesystem;

        if (!exists(subDir_))
            create_directory(subDir_);
        else
            // Cannot run the test. Someone created a file or directory
            // where we want to put our directory
            throw std::runtime_error("Cannot create directory: " + subDir_.string());
    }
    ~KeyFileGuard()
    {
        try
        {
            using namespace boost::filesystem;

            rmDir(subDir_);
        }
        catch (std::exception& e)
        {
            // if we throw here, just let it die.
            test_.log << "Error in ~KeyFileGuard: " << e.what() << std::endl;
        };
    }
};

}  // namespace xrpl
