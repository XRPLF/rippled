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

#include <test/jtx/impl/Account.cpp>
#include <test/jtx/impl/amount.cpp>
#include <test/jtx/impl/balance.cpp>
#include <test/jtx/impl/delivermin.cpp>
#include <test/jtx/impl/Env.cpp>
#include <test/jtx/impl/fee.cpp>
#include <test/jtx/impl/flags.cpp>
#include <test/jtx/impl/jtx_json.cpp>
#include <test/jtx/impl/memo.cpp>
#include <test/jtx/impl/multisign.cpp>
#include <test/jtx/impl/offer.cpp>
#include <test/jtx/impl/owners.cpp>
#include <test/jtx/impl/paths.cpp>
#include <test/jtx/impl/pay.cpp>
#include <test/jtx/impl/quality2.cpp>
#include <test/jtx/impl/rate.cpp>
#include <test/jtx/impl/regkey.cpp>
#include <test/jtx/impl/sendmax.cpp>
#include <test/jtx/impl/seq.cpp>
#include <test/jtx/impl/sig.cpp>
#include <test/jtx/impl/tag.cpp>
#include <test/jtx/impl/ticket.cpp>
#include <test/jtx/impl/trust.cpp>
#include <test/jtx/impl/txflags.cpp>
#include <test/jtx/impl/utility.cpp>

#include <test/jtx/impl/JSONRPCClient.cpp>
#include <test/jtx/impl/ManualTimeKeeper.cpp>
#include <test/jtx/impl/WSClient.cpp>
#include <test/jtx/Env_test.cpp>
#include <test/jtx/WSClient_test.cpp>
