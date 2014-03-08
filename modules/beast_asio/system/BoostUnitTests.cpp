//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

namespace beast {

/** Test for showing information about the build of boost.
*/
class BoostUnitTests : public UnitTest
{
public:
    struct BoostVersion
    {
        explicit BoostVersion (int value)
            : vmajor (value / 100000)
            , vminor ((value / 100) % 100)
            , vpatch (value % 100)
        {
        }

        String toString () const noexcept
        {
            return String (vmajor) + "." +
                   String (vminor).paddedLeft ('0', 2) + "." +
                   String (vpatch).paddedLeft ('0', 2);
        }

        int vmajor;
        int vminor;
        int vpatch;
    };

    enum
    {
        minimumVersion = 104700
    };

    // To prevent constant conditional expression warning
    static int getMinimumVersion ()
    {
        return minimumVersion;
    }

    void runTest ()
    {
        beginTestCase ("version");

        BoostVersion version (BOOST_VERSION);

        logMessage (String ("BOOST_VERSION = " + version.toString ()));
        logMessage (String ("BOOST_LIB_VERSION = '") + BOOST_LIB_VERSION + "'");

        if (BOOST_VERSION >= getMinimumVersion ())
        {
            pass ();
        }
        else
        {
            fail (String ("Boost version is below ") +
                BoostVersion (minimumVersion).toString ());
        }
    }

    BoostUnitTests () : UnitTest ("boost", "beast")
    {
    }
};

static BoostUnitTests boostUnitTests;

}
