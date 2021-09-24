//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/app/sidechain/impl/DoorKeeper.h>

#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/impl/TicketHolder.h>
#include <ripple/basics/Log.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace sidechain {

DoorKeeper::DoorKeeper(
    bool isMainChain,
    AccountID const& account,
    TicketRunner& ticketRunner,
    Federator& federator,
    beast::Journal j)
    : isMainChain_(isMainChain)
    , accountStr_(toBase58(account))
    , ticketRunner_(ticketRunner)
    , federator_(federator)
    , j_(j)
{
}

void
DoorKeeper::init()
{
    std::lock_guard lock(mtx_);
    if (initData_.status_ != InitializeStatus::waitLedger)
        return;
    initData_.status_ = InitializeStatus::waitAccountInfo;
    rpcAccountInfo(lock);
}

void
DoorKeeper::updateQueueLength(std::uint32_t length)
{
    DoorStatus oldStatus;
    auto const tx = [&]() -> std::optional<Json::Value> {
        enum Action { setFlag, clearFlag, noAction };
        Action action = noAction;
        std::lock_guard lock(mtx_);
        JLOGV(
            j_.trace(),
            "updateQueueLength",
            jv("account:", accountStr_),
            jv("QLen", length),
            jv("chain", (isMainChain_ ? "main" : "side")));

        if (initData_.status_ != InitializeStatus::initialized)
            return {};

        oldStatus = status_;
        if (length >= HighWaterMark && status_ == DoorStatus::open)
        {
            action = setFlag;
            status_ = DoorStatus::closing;
        }
        else if (length <= LowWaterMark && status_ == DoorStatus::closed)
        {
            action = clearFlag;
            status_ = DoorStatus::opening;
        }

        if (action == noAction)
            return {};

        XRPAmount const fee{Federator::accountControlTxFee};
        Json::Value txJson;
        txJson[jss::TransactionType] = "AccountSet";
        txJson[jss::Account] = accountStr_;
        txJson[jss::Sequence] = 0;  // to be filled by ticketRunner
        txJson[jss::Fee] = to_string(fee);
        if (action == setFlag)
            txJson[jss::SetFlag] = asfDepositAuth;
        else
            txJson[jss::ClearFlag] = asfDepositAuth;
        return txJson;
    }();

    if (tx)
    {
        bool triggered = false;
        if (isMainChain_)
        {
            triggered =
                ticketRunner_.trigger(TicketPurpose::mainDoorKeeper, tx, {});
        }
        else
        {
            triggered =
                ticketRunner_.trigger(TicketPurpose::sideDoorKeeper, {}, tx);
        }

        JLOGV(
            j_.trace(),
            "updateQueueLength",
            jv("account:", accountStr_),
            jv("QLen", length),
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("tx", *tx),
            jv("triggered", (triggered ? "yes" : "no")));

        if (!triggered)
        {
            std::lock_guard lock(mtx_);
            status_ = oldStatus;
        }
    }
}

void
DoorKeeper::onEvent(const event::DepositAuthResult& e)
{
    std::lock_guard lock(mtx_);
    if (initData_.status_ != InitializeStatus::initialized)
    {
        JLOG(j_.trace()) << "Queue an event";
        initData_.toReplay_.push(e);
    }
    else
    {
        processEvent(e, lock);
    }
}

void
DoorKeeper::rpcAccountInfo(std::lock_guard<std::mutex> const&)
{
    Json::Value params = [&] {
        Json::Value r;
        r[jss::account] = accountStr_;
        r[jss::ledger_index] = "validated";
        r[jss::signer_lists] = false;
        return r;
    }();

    rpcChannel_->send(
        "account_info",
        params,
        [chain = isMainChain_ ? Federator::mainChain : Federator::sideChain,
         wp = federator_.weak_from_this()](Json::Value const& response) {
            if (auto f = wp.lock())
                f->getDoorKeeper(chain).accountInfoResult(response);
        });
}

void
DoorKeeper::accountInfoResult(const Json::Value& rpcResult)
{
    auto ledgerNFlagsOpt =
        [&]() -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
        try
        {
            if (rpcResult.isMember(jss::error))
            {
                return {};
            }
            if (!rpcResult[jss::validated].asBool())
            {
                return {};
            }
            if (rpcResult[jss::account_data][jss::Account] != accountStr_)
            {
                return {};
            }
            if (!rpcResult[jss::account_data][jss::Flags].isIntegral())
            {
                return {};
            }
            if (!rpcResult[jss::ledger_index].isIntegral())
            {
                return {};
            }

            return std::make_pair(
                rpcResult[jss::ledger_index].asUInt(),
                rpcResult[jss::account_data][jss::Flags].asUInt());
        }
        catch (...)
        {
            return {};
        }
    }();

    if (!ledgerNFlagsOpt)
    {
        // should not reach here since we only ask account_object after a
        // validated ledger
        JLOGV(j_.error(), "account_info result ", jv("result", rpcResult));
        assert(false);
        return;
    }

    auto [ledgerIndex, flags] = *ledgerNFlagsOpt;
    {
        JLOGV(
            j_.trace(),
            "accountInfoResult",
            jv("ledgerIndex", ledgerIndex),
            jv("flags", flags));
        std::lock_guard lock(mtx_);
        initData_.ledgerIndex_ = ledgerIndex;
        status_ = (flags & lsfDepositAuth) == 0 ? DoorStatus::open
                                                : DoorStatus::closed;
        while (!initData_.toReplay_.empty())
        {
            processEvent(initData_.toReplay_.front(), lock);
            initData_.toReplay_.pop();
        }
        initData_.status_ = InitializeStatus::initialized;
        JLOG(j_.info()) << "DoorKeeper initialized, status "
                        << (status_ == DoorStatus::open ? "open" : "closed");
    }
}

void
DoorKeeper::processEvent(
    const event::DepositAuthResult& e,
    std::lock_guard<std::mutex> const&)
{
    if (e.ledgerIndex_ <= initData_.ledgerIndex_)
    {
        JLOGV(
            j_.trace(),
            "DepositAuthResult, ignoring an old result",
            jv("account:", accountStr_),
            jv("operation",
               (e.op_ == event::AccountFlagOp::set ? "set" : "clear")));
        return;
    }

    JLOGV(
        j_.trace(),
        "DepositAuthResult",
        jv("chain", (isMainChain_ ? "main" : "side")),
        jv("account:", accountStr_),
        jv("operation",
           (e.op_ == event::AccountFlagOp::set ? "set" : "clear")));

    if (!e.success_)
    {
        JLOG(j_.error()) << "DepositAuthResult event error, account "
                         << (isMainChain_ ? "main" : "side") << accountStr_;
        assert(false);
        return;
    }

    switch (e.op_)
    {
        case event::AccountFlagOp::set:
            assert(
                status_ == DoorStatus::open || status_ == DoorStatus::closing);
            status_ = DoorStatus::closed;
            break;
        case event::AccountFlagOp::clear:
            assert(
                status_ == DoorStatus::closed ||
                status_ == DoorStatus::opening);
            status_ = DoorStatus::open;
            break;
    }
}

Json::Value
DoorKeeper::getInfo() const
{
    auto DoorStatusToStr = [](DoorKeeper::DoorStatus s) -> std::string {
        switch (s)
        {
            case DoorKeeper::DoorStatus::open:
                return "open";
            case DoorKeeper::DoorStatus::opening:
                return "opening";
            case DoorKeeper::DoorStatus::closed:
                return "closed";
            case DoorKeeper::DoorStatus::closing:
                return "closing";
        }
        return {};
    };

    Json::Value ret{Json::objectValue};
    {
        std::lock_guard lock{mtx_};
        if (initData_.status_ == InitializeStatus::initialized)
        {
            ret["initialized"] = "true";
            ret["status"] = DoorStatusToStr(status_);
        }
        else
        {
            ret["initialized"] = "false";
        }
    }
    return ret;
}

void
DoorKeeper::setRpcChannel(std::shared_ptr<ChainListener> channel)
{
    rpcChannel_ = std::move(channel);
}

}  // namespace sidechain
}  // namespace ripple
