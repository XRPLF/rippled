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
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_TESTSUITE_H_INCLUDED
#define RIPPLE_BASICS_TESTSUITE_H_INCLUDED

#include <ripple/beast/unit_test.h>
#include <string>

namespace ripple {

class TestSuite : public beast::unit_test::suite
{
public:
    template <class S, class T>
    bool expectEquals (S actual, T expected, std::string const& message = "")
    {
        if (actual != expected)
        {
            std::stringstream ss;
            if (!message.empty())
                ss << message << "\n";
            ss << "Actual: " << actual << "\n"
               << "Expected: " << expected;
            fail (ss.str());
            return false;
        }
        pass ();
        return true;

    }

    template <class S, class T>
    bool expectNotEquals(S actual, T expected, std::string const& message = "")
    {
        if (actual == expected)
        {
            std::stringstream ss;
            if (!message.empty())
                ss << message << "\n";
            ss << "Actual: " << actual << "\n"
                << "Expected anything but: " << expected;
            fail(ss.str());
            return false;
        }
        pass();
        return true;

    }

    template <class Collection>
    bool expectCollectionEquals (
        Collection const& actual, Collection const& expected,
        std::string const& message = "")
    {
        auto msg = addPrefix (message);
        bool success = expectEquals (actual.size(), expected.size(),
                                     msg + "Sizes are different");
        using std::begin;
        using std::end;

        auto i = begin (actual);
        auto j = begin (expected);
        auto k = 0;

        for (; i != end (actual) && j != end (expected); ++i, ++j, ++k)
        {
            if (!expectEquals (*i, *j, msg + "Elements at " +
                               std::to_string(k) + " are different."))
                return false;
        }

        return success;
    }

    template <class Exception, class Functor>
    bool expectException (Functor f, std::string const& message = "")
    {
        bool success = false;
        try
        {
            f();
        } catch (Exception const&)
        {
            success = true;
        }
        return expect (success, addPrefix (message) + "no exception thrown");
    }

    template <class Functor>
    bool expectException (Functor f, std::string const& message = "")
    {
        bool success = false;
        try
        {
            f();
        }
        catch (std::exception const&)
        {
            success = true;
        }
        return expect (success, addPrefix (message) + "no exception thrown");
    }

private:
    static std::string addPrefix (std::string const& message)
    {
        std::string msg = message;
        if (!msg.empty())
            msg = ": " + msg;
        return msg;
    }
};

} // ripple

#endif
