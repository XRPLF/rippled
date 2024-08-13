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

#include <xrpld/overlay/detail/ProtocolMessage.h>
#include <xrpl/protocol/messages.h>
#include <xrpl/beast/unit_test.h>

namespace ripple {

class ProtocolMessage_test : public beast::unit_test::suite
{
public:
    void testSuccessfulHashGeneration()
    {
        protocol::TMLedgerData msg;
        msg.set_ledgerhash("test_hash");
        msg.set_ledgerseq(12345);
        msg.set_type(::protocol::TMLedgerInfoType::liTS_CANDIDATE);

        auto const [hash, error] = hashProtoBufMessage(msg);

        BEAST_EXPECT(hash.has_value());
        BEAST_EXPECT(error.empty());
    }

    void testPartialInitilisationHandling()
    {
        protocol::TMLedgerData msg;
        msg.set_ledgerhash("test_hash");
        
        auto const [hash, error] = hashProtoBufMessage(msg);

        BEAST_EXPECT(!hash.has_value());
        BEAST_EXPECT(!error.empty());
    }

    void testEmptyMessageHandling()
    {
        protocol::TMLedgerData msg;

        auto const [hash, error] = hashProtoBufMessage(msg);

        BEAST_EXPECT(!hash.has_value());
        BEAST_EXPECT(!error.empty());
    }

    void run() override
    {
        testSuccessfulHashGeneration();
        testPartialInitilisationHandling();
        testEmptyMessageHandling();
    }
};

BEAST_DEFINE_TESTSUITE(ProtocolMessage, overlay, ripple);

}  // namespace ripple
