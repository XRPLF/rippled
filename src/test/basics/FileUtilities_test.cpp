//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/ByteUtilities.h>
#include <ripple/basics/FileUtilities.h>
#include <ripple/beast/unit_test.h>
#include <test/unit_test/FileDirGuard.h>

namespace ripple {

class FileUtilities_test : public beast::unit_test::suite
{
public:
    void
    testGetFileContents()
    {
        using namespace ripple::test::detail;
        using namespace boost::system;

        constexpr const char* expectedContents =
            "This file is very short. That's all we need.";

        FileDirGuard file(
            *this,
            "test_file",
            "test.txt",
            "This is temporary text that should get overwritten");

        error_code ec;
        auto const path = file.file();

        writeFileContents(ec, path, expectedContents);
        BEAST_EXPECT(!ec);

        {
            // Test with no max
            auto const good = getFileContents(ec, path);
            BEAST_EXPECT(!ec);
            BEAST_EXPECT(good == expectedContents);
        }

        {
            // Test with large max
            auto const good = getFileContents(ec, path, kilobytes(1));
            BEAST_EXPECT(!ec);
            BEAST_EXPECT(good == expectedContents);
        }

        {
            // Test with small max
            auto const bad = getFileContents(ec, path, 16);
            BEAST_EXPECT(
                ec && ec.value() == boost::system::errc::file_too_large);
            BEAST_EXPECT(bad.empty());
        }
    }

    void
    run() override
    {
        testGetFileContents();
    }
};

BEAST_DEFINE_TESTSUITE(FileUtilities, ripple_basics, ripple);

}  // namespace ripple
