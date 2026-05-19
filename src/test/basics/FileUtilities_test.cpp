#include <test/unit_test/FileDirGuard.h>

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/beast/unit_test/suite.h>

#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>

namespace xrpl {

class FileUtilities_test : public beast::unit_test::Suite
{
public:
    void
    testGetFileContents()
    {
        using namespace xrpl::detail;
        using namespace boost::system;

        static constexpr char const* kExpectedContents =
            "This file is very short. That's all we need.";

        FileDirGuard const file(
            *this, "test_file", "test.txt", "This is temporary text that should get overwritten");

        error_code ec;
        auto const path = file.file();

        writeFileContents(ec, path, kExpectedContents);
        BEAST_EXPECT(!ec);

        {
            // Test with no max
            auto const good = getFileContents(ec, path);
            BEAST_EXPECT(!ec);
            BEAST_EXPECT(good == kExpectedContents);
        }

        {
            // Test with large max
            auto const good = getFileContents(ec, path, kilobytes(1));
            BEAST_EXPECT(!ec);
            BEAST_EXPECT(good == kExpectedContents);
        }

        {
            // Test with small max
            auto const bad = getFileContents(ec, path, 16);
            BEAST_EXPECT(ec && ec.value() == boost::system::errc::file_too_large);
            BEAST_EXPECT(bad.empty());
        }
    }

    void
    run() override
    {
        testGetFileContents();
    }
};

BEAST_DEFINE_TESTSUITE(FileUtilities, basics, xrpl);

}  // namespace xrpl
