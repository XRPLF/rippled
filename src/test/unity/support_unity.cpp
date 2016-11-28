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

#include <BeastConfig.h>

#include <test/support/jtx/impl/Account.cpp>
#include <test/support/jtx/impl/amount.cpp>
#include <test/support/jtx/impl/balance.cpp>
#include <test/support/jtx/impl/delivermin.cpp>
#include <test/support/jtx/impl/Env.cpp>
#include <test/support/jtx/impl/fee.cpp>
#include <test/support/jtx/impl/flags.cpp>
#include <test/support/jtx/impl/jtx_json.cpp>
#include <test/support/jtx/impl/memo.cpp>
#include <test/support/jtx/impl/multisign.cpp>
#include <test/support/jtx/impl/offer.cpp>
#include <test/support/jtx/impl/owners.cpp>
#include <test/support/jtx/impl/paths.cpp>
#include <test/support/jtx/impl/pay.cpp>
#include <test/support/jtx/impl/quality2.cpp>
#include <test/support/jtx/impl/rate.cpp>
#include <test/support/jtx/impl/regkey.cpp>
#include <test/support/jtx/impl/sendmax.cpp>
#include <test/support/jtx/impl/seq.cpp>
#include <test/support/jtx/impl/sig.cpp>
#include <test/support/jtx/impl/tag.cpp>
#include <test/support/jtx/impl/ticket.cpp>
#include <test/support/jtx/impl/trust.cpp>
#include <test/support/jtx/impl/txflags.cpp>
#include <test/support/jtx/impl/utility.cpp>

#include <test/support/mao/impl/Net.cpp>
#include <test/support/impl/JSONRPCClient.cpp>
#include <test/support/impl/ManualTimeKeeper.cpp>
#include <test/support/impl/WSClient.cpp>

#include <test/support/BasicNetwork_test.cpp>
#include <test/support/Env_test.cpp>
#include <test/support/WSClient_test.cpp>
