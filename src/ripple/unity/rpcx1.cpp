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


// This has to be included early to prevent an obscure MSVC compile error
#include <boost/asio/deadline_timer.hpp>

#include <ripple/protocol/jss.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/handlers/Handlers.h>

#include <ripple/rpc/handlers/AccountCurrenciesHandler.cpp>
#include <ripple/rpc/handlers/AccountInfo.cpp>
#include <ripple/rpc/handlers/AccountLines.cpp>
#include <ripple/rpc/handlers/AccountChannels.cpp>
#include <ripple/rpc/handlers/AccountObjects.cpp>
#include <ripple/rpc/handlers/AccountOffers.cpp>
#include <ripple/rpc/handlers/AccountTx.cpp>
#include <ripple/rpc/handlers/AccountTxOld.cpp>
#include <ripple/rpc/handlers/AccountTxSwitch.cpp>
#include <ripple/rpc/handlers/BlackList.cpp>
#include <ripple/rpc/handlers/BookOffers.cpp>
#include <ripple/rpc/handlers/CanDelete.cpp>
#include <ripple/rpc/handlers/Connect.cpp>
#include <ripple/rpc/handlers/ConsensusInfo.cpp>
#include <ripple/rpc/handlers/CrawlShards.cpp>
#include <ripple/rpc/handlers/DepositAuthorized.cpp>
#include <ripple/rpc/handlers/DownloadShard.cpp>
#include <ripple/rpc/handlers/Feature1.cpp>
#include <ripple/rpc/handlers/Fee1.cpp>
#include <ripple/rpc/handlers/FetchInfo.cpp>
#include <ripple/rpc/handlers/GatewayBalances.cpp>
#include <ripple/rpc/handlers/GetCounts.cpp>
#include <ripple/rpc/handlers/LedgerHandler.cpp>
#include <ripple/rpc/handlers/LedgerAccept.cpp>
#include <ripple/rpc/handlers/LedgerCleanerHandler.cpp>
#include <ripple/rpc/handlers/LedgerClosed.cpp>
#include <ripple/rpc/handlers/LedgerCurrent.cpp>
#include <ripple/rpc/handlers/LedgerData.cpp>
#include <ripple/rpc/handlers/LedgerEntry.cpp>
#include <ripple/rpc/handlers/LedgerHeader.cpp>
#include <ripple/rpc/handlers/LedgerRequest.cpp>
#include <ripple/rpc/handlers/LogLevel.cpp>
#include <ripple/rpc/handlers/LogRotate.cpp>
#include <ripple/rpc/handlers/Manifest.cpp>
#include <ripple/rpc/handlers/NoRippleCheck.cpp>
#include <ripple/rpc/handlers/OwnerInfo.cpp>
#include <ripple/rpc/handlers/ValidatorInfo.cpp>
