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

#include <ripple/app/sidechain/impl/TicketHolder.h>

#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/impl/SignatureCollector.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/STParsedJSON.h>

namespace ripple {
namespace sidechain {

std::string
TicketPurposeToStr(TicketPurpose tp)
{
    switch (tp)
    {
        case TicketPurpose::mainDoorKeeper:
            return "mainDoorKeeper";
        case TicketPurpose::sideDoorKeeper:
            return "sideDoorKeeper";
        case TicketPurpose::updateSignerList:
            return "updateSignerList";
        default:
            break;
    }
    return "unknown";
}

TicketHolder::TicketHolder(
    bool isMainChain,
    AccountID const& account,
    Federator& federator,
    beast::Journal j)
    : isMainChain_(isMainChain)
    , accountStr_(toBase58(account))
    , federator_(federator)
    , j_(j)
{
}

void
TicketHolder::init()
{
    std::lock_guard lock(mtx_);
    if (initData_.status_ != InitializeStatus::waitLedger)
        return;
    initData_.status_ = InitializeStatus::waitAccountObject;
    rpcAccountObject();
}

std::optional<std::uint32_t>
TicketHolder::getTicket(TicketPurpose purpose, PeekOrTake pt)
{
    std::lock_guard lock(mtx_);

    if (initData_.status_ != InitializeStatus::initialized)
    {
        JLOGV(
            j_.debug(),
            "TicketHolder getTicket but ticket holder not initialized",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("purpose", TicketPurposeToStr(purpose)));

        if (initData_.status_ == InitializeStatus::needToQueryTx)
            rpcTx(lock);
        return {};
    }

    auto index = static_cast<std::underlying_type_t<TicketPurpose>>(purpose);
    if (tickets_[index].status_ == AutoRenewedTicket::Status::available)
    {
        if (pt == PeekOrTake::take)
        {
            JLOGV(
                j_.trace(),
                "getTicket",
                jv("chain", (isMainChain_ ? "main" : "side")),
                jv("seq", tickets_[index].seq_));
            tickets_[index].status_ = AutoRenewedTicket::Status::taken;
        }
        return tickets_[index].seq_;
    }
    if (pt == PeekOrTake::take)
    {
        JLOGV(
            j_.trace(),
            "getTicket",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("no ticket for ", TicketPurposeToStr(purpose)));
    }
    return {};
}

void
TicketHolder::onEvent(event::TicketCreateResult const& e)
{
    std::lock_guard lock(mtx_);
    if (initData_.status_ != InitializeStatus::initialized)
    {
        JLOG(j_.trace()) << "TicketHolder queues an event";
        initData_.toReplay_.emplace(e);
        return;
    }
    processEvent(e, lock);
}

void
TicketHolder::onEvent(event::BootstrapTicket const& e)
{
    std::lock_guard lock(mtx_);
    if (initData_.status_ != InitializeStatus::initialized)
    {
        JLOG(j_.trace()) << "TicketHolder queues an event";
        initData_.bootstrapTicketToReplay_.emplace(e);
        return;
    }
    processEvent(e, lock);
}

Json::Value
TicketHolder::getInfo() const
{
    Json::Value ret{Json::objectValue};
    {
        std::lock_guard lock{mtx_};
        if (initData_.status_ == InitializeStatus::initialized)
        {
            ret["initialized"] = "true";
            Json::Value tickets{Json::arrayValue};
            for (auto const& t : tickets_)
            {
                Json::Value tj{Json::objectValue};
                tj["ticket_seq"] = t.seq_;
                tj["status"] = t.status_ == AutoRenewedTicket::Status::taken
                    ? "taken"
                    : "available";
                tickets.append(tj);
            }
            ret["tickets"] = tickets;
        }
        else
        {
            ret["initialized"] = "false";
        }
    }
    return ret;
}

void
TicketHolder::rpcAccountObject()
{
    Json::Value params = [&] {
        Json::Value r;
        r[jss::account] = accountStr_;
        r[jss::ledger_index] = "validated";
        r[jss::type] = "ticket";
        r[jss::limit] = 250;
        return r;
    }();

    rpcChannel_->send(
        "account_objects",
        params,
        [isMainChain = isMainChain_,
         f = federator_.weak_from_this()](Json::Value const& response) {
            auto federator = f.lock();
            if (!federator)
                return;
            federator->getTicketRunner().accountObjectResult(
                isMainChain, response);
        });
}

void
TicketHolder::accountObjectResult(Json::Value const& rpcResult)
{
    auto ledgerNAccountObjectOpt =
        [&]() -> std::optional<std::pair<std::uint32_t, Json::Value>> {
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
            if (rpcResult[jss::account] != accountStr_)
            {
                return {};
            }
            if (!rpcResult[jss::ledger_index].isIntegral())
            {
                return {};
            }
            if (!rpcResult.isMember(jss::account_objects) ||
                !rpcResult[jss::account_objects].isArray())
            {
                return {};
            }
            return std::make_pair(
                rpcResult[jss::ledger_index].asUInt(),
                rpcResult[jss::account_objects]);
        }
        catch (...)
        {
            return {};
        }
    }();

    if (!ledgerNAccountObjectOpt)
    {
        // can reach here?
        // should not since we only ask account_object after a validated ledger
        JLOGV(j_.error(), "AccountObject", jv("result", rpcResult));
        assert(false);
        return;
    }

    auto& [ledgerIndex, accountObjects] = *ledgerNAccountObjectOpt;
    std::lock_guard<std::mutex> lock(mtx_);
    if (initData_.status_ != InitializeStatus::waitAccountObject)
    {
        JLOG(j_.warn()) << "unexpected AccountObject";
        return;
    }

    initData_.ledgerIndex_ = ledgerIndex;
    for (auto const& o : accountObjects)
    {
        if (!o.isMember("LedgerEntryType") ||
            o["LedgerEntryType"] != jss::Ticket)
            continue;
        // the following fields are mandatory
        uint256 txHash;
        if (!txHash.parseHex(o["PreviousTxnID"].asString()))
        {
            JLOGV(
                j_.error(),
                "AccountObject cannot parse tx hash",
                jv("result", rpcResult));
            assert(false);
            return;
        }
        std::uint32_t ticketSeq = o["TicketSequence"].asUInt();
        if (initData_.tickets_.find(txHash) != initData_.tickets_.end())
        {
            JLOGV(
                j_.error(),
                "AccountObject cannot parse tx hash",
                jv("result", rpcResult));
            assert(false);
            return;
        }
        initData_.tickets_.emplace(txHash, ticketSeq);
        JLOGV(
            j_.trace(),
            "AccountObject, add",
            jv("tx hash", txHash),
            jv("ticketSeq", ticketSeq));
    }

    if (initData_.tickets_.empty())
    {
        JLOG(j_.debug()) << "Door account has no tickets in current ledger, "
                            "unlikely but could happen";
        replay(lock);
    }
    else
    {
        rpcTx(lock);
    }
}

void
TicketHolder::rpcTx(std::lock_guard<std::mutex> const&)
{
    assert(!initData_.tickets_.empty());
    initData_.status_ = InitializeStatus::waitTx;
    for (auto const& t : initData_.tickets_)
    {
        JLOG(j_.trace()) << "TicketHolder query tx " << t.first;
        Json::Value params;
        params[jss::transaction] = strHex(t.first);
        rpcChannel_->send(
            "tx",
            params,
            [isMainChain = isMainChain_,
             f = federator_.weak_from_this()](Json::Value const& response) {
                auto federator = f.lock();
                if (!federator)
                    return;
                federator->getTicketRunner().txResult(isMainChain, response);
            });
    }
}

void
TicketHolder::txResult(Json::Value const& rpcResult)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (initData_.status_ != InitializeStatus::waitTx &&
        initData_.status_ != InitializeStatus::needToQueryTx)
        return;
    auto txOpt = [&]() -> std::optional<std::pair<TicketPurpose, uint256>> {
        try
        {
            if (rpcResult.isMember(jss::error))
            {
                return {};
            }
            if (rpcResult[jss::Account] != accountStr_)
            {
                return {};
            }

            if (rpcResult[jss::TransactionType] != "TicketCreate")
            {
                return {};
            }

            if (!rpcResult["SourceTag"].isIntegral())
            {
                return {};
            }
            std::uint32_t tp = rpcResult["SourceTag"].asUInt();
            if (tp >= static_cast<std::underlying_type_t<TicketPurpose>>(
                          TicketPurpose::TP_NumberOfItems))
            {
                return {};
            }

            uint256 txHash;
            if (!txHash.parseHex(rpcResult[jss::hash].asString()))
            {
                return {};
            }
            return std::make_pair(static_cast<TicketPurpose>(tp), txHash);
        }
        catch (...)
        {
            return {};
        }
    }();

    if (!txOpt)
    {
        JLOGV(
            j_.warn(),
            "TicketCreate can not be found or has wrong format",
            jv("result", rpcResult));
        if (initData_.status_ == InitializeStatus::waitTx)
            initData_.status_ = InitializeStatus::needToQueryTx;
        return;
    }

    auto [tPurpose, txHash] = *txOpt;
    if (initData_.tickets_.find(txHash) == initData_.tickets_.end())
    {
        JLOGV(
            j_.debug(),
            "Repeated TicketCreate tx result",
            jv("result", rpcResult));
        return;
    }

    auto& ticket = initData_.tickets_[txHash];
    JLOGV(
        j_.trace(),
        "TicketHolder txResult",
        jv("purpose", TicketPurposeToStr(tPurpose)),
        jv("txHash", txHash));

    auto index = static_cast<std::underlying_type_t<TicketPurpose>>(tPurpose);
    tickets_[index].seq_ = ticket;
    tickets_[index].status_ = AutoRenewedTicket::Status::available;
    initData_.tickets_.erase(txHash);

    if (initData_.tickets_.empty())
    {
        replay(lock);
    }
}

void
TicketHolder::replay(std::lock_guard<std::mutex> const& lock)
{
    assert(initData_.tickets_.empty());
    // replay bootstrap tickets first if any
    while (!initData_.bootstrapTicketToReplay_.empty())
    {
        auto e = initData_.bootstrapTicketToReplay_.front();
        processEvent(e, lock);
        initData_.bootstrapTicketToReplay_.pop();
    }

    while (!initData_.toReplay_.empty())
    {
        auto e = initData_.toReplay_.front();
        processEvent(e, lock);
        initData_.toReplay_.pop();
    }
    initData_.status_ = InitializeStatus::initialized;
    JLOG(j_.info()) << "TicketHolder initialized";
}

template <class E>
void
TicketHolder::processEvent(E const& e, std::lock_guard<std::mutex> const&)
{
    std::uint32_t const tSeq = e.txnSeq_ + 1;
    if (e.sourceTag_ >= static_cast<std::underlying_type_t<TicketPurpose>>(
                            TicketPurpose::TP_NumberOfItems))
    {
        JLOGV(
            j_.error(),
            "Wrong sourceTag",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("sourceTag", e.sourceTag_));
        assert(false);
        return;
    }

    auto purposeStr =
        TicketPurposeToStr(static_cast<TicketPurpose>(e.sourceTag_));

    if (e.ledgerIndex_ <= initData_.ledgerIndex_)
    {
        JLOGV(
            j_.trace(),
            "TicketHolder, ignoring an old ticket",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("ticket seq", tSeq),
            jv("purpose", purposeStr));
        return;
    }

    if (!e.success_)
    {
        JLOGV(
            j_.error(),
            "CreateTicket failed",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("ticket seq", tSeq),
            jv("purpose", purposeStr));
        assert(false);
        return;
    }

    JLOGV(
        j_.trace(),
        "TicketHolder, got a ticket",
        jv("chain", (isMainChain_ ? "main" : "side")),
        jv("ticket seq", tSeq),
        jv("purpose", purposeStr));

    std::uint32_t const ticketPurposeToIndex = e.sourceTag_;

    if (e.eventType() == event::EventType::bootstrap &&
        tickets_[ticketPurposeToIndex].seq_ != 0)
    {
        JLOGV(
            j_.error(),
            "Got a bootstrap ticket too late",
            jv("chain", (isMainChain_ ? "main" : "side")),
            jv("ticket seq", tSeq),
            jv("purpose", purposeStr));
        assert(false);
        return;
    }
    tickets_[ticketPurposeToIndex].seq_ = tSeq;
    tickets_[ticketPurposeToIndex].status_ =
        AutoRenewedTicket::Status::available;
}

void
TicketHolder::setRpcChannel(std::shared_ptr<ChainListener> channel)
{
    rpcChannel_ = std::move(channel);
}

TicketRunner::TicketRunner(
    const AccountID& mainAccount,
    const AccountID& sideAccount,
    Federator& federator,
    beast::Journal j)
    : mainAccountStr_(toBase58(mainAccount))
    , sideAccountStr_(toBase58(sideAccount))
    , federator_(federator)
    , mainHolder_(true, mainAccount, federator, j)
    , sideHolder_(false, sideAccount, federator, j)
    , j_(j)
{
}

void
TicketRunner::setRpcChannel(
    bool isMainChain,
    std::shared_ptr<ChainListener> channel)
{
    if (isMainChain)
        mainHolder_.setRpcChannel(std::move(channel));
    else
        sideHolder_.setRpcChannel(std::move(channel));
}

void
TicketRunner::init(bool isMainChain)
{
    if (isMainChain)
        mainHolder_.init();
    else
        sideHolder_.init();
}

void
TicketRunner::accountObjectResult(
    bool isMainChain,
    Json::Value const& rpcResult)
{
    if (isMainChain)
        mainHolder_.accountObjectResult(rpcResult);
    else
        sideHolder_.accountObjectResult(rpcResult);
}

void
TicketRunner::txResult(bool isMainChain, Json::Value const& rpcResult)
{
    if (isMainChain)
        mainHolder_.txResult(rpcResult);
    else
        sideHolder_.txResult(rpcResult);
}

bool
TicketRunner::trigger(
    TicketPurpose purpose,
    std::optional<Json::Value> const& mainTxJson,
    std::optional<Json::Value> const& sideTxJson)
{
    if (!mainTxJson && !sideTxJson)
    {
        assert(false);
        return false;
    }

    auto ticketPair =
        [&]() -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!mainHolder_.getTicket(purpose, TicketHolder::PeekOrTake::peek) ||
            !sideHolder_.getTicket(purpose, TicketHolder::PeekOrTake::peek))
        {
            JLOG(j_.trace()) << "TicketRunner tickets no ready";
            return {};
        }
        auto mainTicket =
            mainHolder_.getTicket(purpose, TicketHolder::PeekOrTake::take);
        auto sideTicket =
            sideHolder_.getTicket(purpose, TicketHolder::PeekOrTake::take);
        assert(mainTicket && sideTicket);
        JLOGV(
            j_.trace(),
            "TicketRunner trigger",
            jv("main ticket", *mainTicket),
            jv("side ticket", *sideTicket),
            jv("purpose", TicketPurposeToStr(purpose)));
        return {{*mainTicket, *sideTicket}};
    }();

    if (!ticketPair)
        return false;

    auto sendTriggerTx = [&](std::string const& accountStr,
                             std::uint32_t ticketSequence,
                             std::optional<Json::Value> const& memoJson,
                             SignatureCollector& signatureCollector) {
        XRPAmount const fee{Federator::accountControlTxFee};
        Json::Value txJson;
        txJson[jss::TransactionType] = "AccountSet";
        txJson[jss::Account] = accountStr;
        txJson[jss::Sequence] = 0;
        txJson[jss::Fee] = to_string(fee);
        txJson["SourceTag"] =
            static_cast<std::underlying_type_t<TicketPurpose>>(purpose);
        txJson["TicketSequence"] = ticketSequence;
        if (memoJson)
        {
            Serializer s;
            try
            {
                STParsedJSONObject parsed(std::string(jss::tx_json), *memoJson);
                if (!parsed.object)
                {
                    JLOGV(
                        j_.fatal(), "invalid transaction", jv("tx", *memoJson));
                    assert(0);
                    return;
                }
                parsed.object->setFieldVL(sfSigningPubKey, Slice(nullptr, 0));
                parsed.object->add(s);
            }
            catch (...)
            {
                JLOGV(j_.fatal(), "invalid transaction", jv("tx", *memoJson));
                assert(0);
                return;
            }

            Json::Value memos{Json::arrayValue};
            Json::Value memo;
            auto const dataStr = strHex(s.peekData());
            memo[jss::Memo][jss::MemoData] = dataStr;
            memos.append(memo);
            txJson[jss::Memos] = memos;
            JLOGV(
                j_.trace(),
                "TicketRunner",
                jv("tx", *memoJson),
                jv("tx packed", dataStr),
                jv("packed size", dataStr.length()));
            assert(
                memo[jss::Memo][jss::MemoData].asString().length() <=
                event::MemoStringMax);
        }
        signatureCollector.signAndSubmit(txJson);
    };

    sendTriggerTx(
        mainAccountStr_,
        ticketPair->first,
        mainTxJson,
        federator_.getSignatureCollector(Federator::ChainType::mainChain));
    sendTriggerTx(
        sideAccountStr_,
        ticketPair->second,
        sideTxJson,
        federator_.getSignatureCollector(Federator::ChainType::sideChain));
    return true;
}

void
TicketRunner::onEvent(
    std::uint32_t accountSeq,
    const event::TicketCreateTrigger& e)
{
    Json::Value txJson;
    XRPAmount const fee{Federator::accountControlTxFee};
    txJson[jss::TransactionType] = "TicketCreate";
    txJson[jss::Account] =
        e.dir_ == event::Dir::mainToSide ? sideAccountStr_ : mainAccountStr_;
    txJson[jss::Sequence] = accountSeq;
    txJson[jss::Fee] = to_string(fee);
    txJson["TicketCount"] = 1;
    txJson["SourceTag"] = e.sourceTag_;
    {
        Json::Value memos{Json::arrayValue};
        {
            Json::Value memo;
            memo[jss::Memo][jss::MemoData] = to_string(e.txnHash_);
            memos.append(memo);
        }
        if (!e.memoStr_.empty())
        {
            Json::Value memo;
            memo[jss::Memo][jss::MemoData] = e.memoStr_;
            memos.append(memo);
        }
        txJson[jss::Memos] = memos;
    }
    JLOGV(
        j_.trace(),
        "TicketRunner TicketTriggerDetected",
        jv("chain", (e.dir_ == event::Dir::mainToSide ? "main" : "side")),
        jv("seq", accountSeq),
        jv("CreateTicket tx", txJson));

    if (e.dir_ == event::Dir::mainToSide)
        federator_.getSignatureCollector(Federator::ChainType::sideChain)
            .signAndSubmit(txJson);
    else
        federator_.getSignatureCollector(Federator::ChainType::mainChain)
            .signAndSubmit(txJson);
}

void
TicketRunner::onEvent(
    std::uint32_t accountSeq,
    const event::TicketCreateResult& e)
{
    auto const [fromChain, toChain] = e.dir_ == event::Dir::mainToSide
        ? std::make_pair(Federator::sideChain, Federator::mainChain)
        : std::make_pair(Federator::mainChain, Federator::sideChain);

    auto ticketSeq = e.txnSeq_ + 1;
    JLOGV(
        j_.trace(),
        "TicketRunner CreateTicketResult",
        jv("chain",
           (fromChain == Federator::ChainType::mainChain ? "main" : "side")),
        jv("ticket seq", ticketSeq));

    if (fromChain == Federator::ChainType::mainChain)
        mainHolder_.onEvent(e);
    else
        sideHolder_.onEvent(e);

    federator_.addSeqToSkip(fromChain, ticketSeq);

    if (accountSeq)
    {
        assert(!e.memoStr_.empty());
        auto txData = strUnHex(e.memoStr_);
        if (!txData || !txData->size())
        {
            assert(false);
            return;
        }
        SerialIter sitTrans(makeSlice(*txData));
        STTx tx(sitTrans);
        tx.setFieldU32(sfSequence, accountSeq);

        auto txJson = tx.getJson(JsonOptions::none);
        // trigger hash
        Json::Value memos{Json::arrayValue};
        {
            Json::Value memo;
            memo[jss::Memo][jss::MemoData] = to_string(e.txnHash_);
            memos.append(memo);
        }
        txJson[jss::Memos] = memos;

        JLOGV(
            j_.trace(),
            "TicketRunner AccountControlTrigger",
            jv("chain",
               (toChain == Federator::ChainType::mainChain ? "side" : "main")),
            jv("tx with added memos", txJson.toStyledString()));

        federator_.getSignatureCollector(toChain).signAndSubmit(txJson);
    }
}

void
TicketRunner::onEvent(const event::BootstrapTicket& e)
{
    auto ticketSeq = e.txnSeq_ + 1;
    JLOGV(
        j_.trace(),
        "TicketRunner BootstrapTicket",
        jv("chain", (e.isMainchain_ ? "main" : "side")),
        jv("ticket seq", ticketSeq));

    if (e.isMainchain_)
        mainHolder_.onEvent(e);
    else
        sideHolder_.onEvent(e);
}

Json::Value
TicketRunner::getInfo(bool isMainchain) const
{
    if (isMainchain)
        return mainHolder_.getInfo();
    else
        return sideHolder_.getInfo();
}

}  // namespace sidechain
}  // namespace ripple
