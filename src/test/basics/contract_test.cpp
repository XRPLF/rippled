//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/contract.h>
#include <ripple/beast/unit_test.h>
#include <string>

namespace ripple {

class contract_test : public beast::unit_test::suite
{
public:
    void run ()
    {
        try
        {
            Throw<std::runtime_error>("Throw test");
        }
        catch (std::runtime_error const& e)
        {
            BEAST_EXPECT(std::string(e.what()) == "Throw test");

            try
            {
                Rethrow();
            }
            catch (std::runtime_error const& e)
            {
                BEAST_EXPECT(std::string(e.what()) == "Throw test");
            }
            catch (...)
            {
                BEAST_EXPECT(false);
            }
        }
        catch (...)
        {
            BEAST_EXPECT(false);
        }
    }
};

BEAST_DEFINE_TESTSUITE(contract,basics,ripple);

}
