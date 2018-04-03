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

#include <ripple/rpc/Status.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/unit_test.h>
#include <algorithm>

namespace ripple {
namespace RPC {

class codeString_test : public beast::unit_test::suite
{
private:
    template <typename Type>
    std::string codeString (Type t)
    {
        return Status (t).codeString();
    }

    void test_OK ()
    {
        testcase ("OK");
        {
            auto s = codeString (Status ());
            expect (s.empty(), "String for OK status");
        }

        {
            auto s = codeString (Status::OK);
            expect (s.empty(), "String for OK status");
        }

        {
            auto s = codeString (0);
            expect (s.empty(), "String for 0 status");
        }

        {
            auto s = codeString (tesSUCCESS);
            expect (s.empty(), "String for tesSUCCESS");
        }

        {
            auto s = codeString (rpcSUCCESS);
            expect (s.empty(), "String for rpcSUCCESS");
        }
    }

    void test_error ()
    {
        testcase ("error");
        {
            auto s = codeString (23);
            expect (s == "23", s);
        }

        {
            auto s = codeString (temBAD_AMOUNT);
            expect (s == "temBAD_AMOUNT: Can only send positive amounts.", s);
        }

        {
            auto s = codeString (rpcBAD_SYNTAX);
            expect (s == "badSyntax: Syntax error.", s);
        }
    }

public:
    void run()
    {
        test_OK ();
        test_error ();
    }
};

BEAST_DEFINE_TESTSUITE (codeString, Status, RPC);

class fillJson_test : public beast::unit_test::suite
{
private:
    Json::Value value_;

    template <typename Type>
    void fillJson (Type t)
    {
        value_.clear ();
        Status (t).fillJson (value_);
    }

    void test_OK ()
    {
        testcase ("OK");
        fillJson (Status ());
        expect (! value_, "Value for empty status");

        fillJson (0);
        expect (! value_, "Value for 0 status");

        fillJson (Status::OK);
        expect (! value_, "Value for OK status");

        fillJson (tesSUCCESS);
        expect (! value_, "Value for tesSUCCESS");

        fillJson (rpcSUCCESS);
        expect (! value_, "Value for rpcSUCCESS");
    }

    template <typename Type>
    void expectFill (
        std::string const& label,
        Type status,
        Status::Strings messages,
        std::string const& message)
    {
        value_.clear ();
        fillJson (Status (status, messages));

        auto prefix = label + ": ";
        expect (bool (value_), prefix + "No value");

        auto error = value_[jss::error];
        expect (bool (error), prefix + "No error.");

        auto code = error[jss::code].asInt();
        expect (status == code, prefix + "Wrong status " +
                std::to_string (code) + " != " + std::to_string (status));

        auto m = error[jss::message].asString ();
        expect (m == message, m + " != " + message);

        auto d = error[jss::data];
        size_t s1 = d.size(), s2 = messages.size();
        expect (s1 == s2, prefix + "Data sizes differ " +
                std::to_string (s1) + " != " + std::to_string (s2));
        for (auto i = 0; i < std::min (s1, s2); ++i)
        {
            auto ds = d[i].asString();
            expect (ds == messages[i], prefix + ds + " != " +  messages[i]);
        }
    }

    void test_error ()
    {
        testcase ("error");
        expectFill (
            "temBAD_AMOUNT",
            temBAD_AMOUNT,
            {},
            "temBAD_AMOUNT: Can only send positive amounts.");

        expectFill (
            "rpcBAD_SYNTAX",
            rpcBAD_SYNTAX,
            {"An error.", "Another error."},
            "badSyntax: Syntax error.");

        expectFill (
            "integer message",
            23,
            {"Stuff."},
            "23");
    }

    void test_throw ()
    {
        testcase ("throw");
        try
        {
            Throw<Status> (Status(temBAD_PATH, { "path=sdcdfd" }));
        }
        catch (Status const& s)
        {
            expect (s.toTER () == temBAD_PATH, "temBAD_PATH wasn't thrown");
            auto msgs = s.messages ();
            expect (msgs.size () == 1, "Wrong number of messages");
            expect (msgs[0] == "path=sdcdfd", msgs[0]);
        }
        catch (std::exception const&)
        {
            expect (false, "Didn't catch a Status");
        }
    }

public:
    void run()
    {
        test_OK ();
        test_error ();
        test_throw ();
    }
};

BEAST_DEFINE_TESTSUITE (fillJson, Status, RPC);

} // namespace RPC
} // ripple
