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

#include <ripple/app/sidechain/impl/ChainListener.h>

#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/app/sidechain/impl/WebsocketClient.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/basics/strHex.h>
#include <ripple/json/Output.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>

#include <type_traits>

namespace ripple {
namespace sidechain {

class Federator;

ChainListener::ChainListener(
    IsMainchain isMainchain,
    AccountID const& account,
    std::weak_ptr<Federator>&& federator,
    beast::Journal j)
    : isMainchain_{isMainchain == IsMainchain::yes}
    , doorAccount_{account}
    , doorAccountStr_{toBase58(account)}
    , federator_{std::move(federator)}
    , initialSync_{std::make_unique<InitialSync>(federator_, isMainchain_, j)}
    , j_{j}
{
}

// destructor must be defined after WebsocketClient size is known (i.e. it can
// not be defaulted in the header or the unique_ptr declration of
// WebsocketClient won't work)
ChainListener::~ChainListener() = default;

std::string const&
ChainListener::chainName() const
{
    // Note: If this function is ever changed to return a value instead of a
    // ref, review the code to ensure the "jv" functions don't bind to temps
    static const std::string m("Mainchain");
    static const std::string s("Sidechain");
    return isMainchain_ ? m : s;
}

namespace detail {
template <class T>
std::optional<T>
getMemoData(Json::Value const& v, std::uint32_t index) = delete;

template <>
std::optional<uint256>
getMemoData<uint256>(Json::Value const& v, std::uint32_t index)
{
    try
    {
        uint256 result;
        if (result.parseHex(
                v[jss::Memos][index][jss::Memo][jss::MemoData].asString()))
            return result;
    }
    catch (...)
    {
    }
    return {};
}

template <>
std::optional<uint8_t>
getMemoData<uint8_t>(Json::Value const& v, std::uint32_t index)
{
    try
    {
        auto const hexData =
            v[jss::Memos][index][jss::Memo][jss::MemoData].asString();
        auto d = hexData.data();
        if (hexData.size() != 2)
            return {};
        auto highNibble = charUnHex(d[0]);
        auto lowNibble = charUnHex(d[1]);
        if (highNibble < 0 || lowNibble < 0)
            return {};
        return (highNibble << 4) | lowNibble;
    }
    catch (...)
    {
    }
    return {};
}

}  // namespace detail

template <class E>
void
ChainListener::pushEvent(
    E&& e,
    int txHistoryIndex,
    std::lock_guard<std::mutex> const&)
{
    static_assert(std::is_rvalue_reference_v<decltype(e)>, "");

    if (initialSync_)
    {
        auto const hasReplayed = initialSync_->onEvent(std::move(e));
        if (hasReplayed)
            initialSync_.reset();
    }
    else if (auto f = federator_.lock(); f && txHistoryIndex >= 0)
    {
        f->push(std::move(e));
    }
}

void
ChainListener::processMessage(Json::Value const& msg)
{
    // Even though this lock has a large scope, this function does very little
    // processing and should run relatively quickly
    std::lock_guard l{m_};

    JLOGV(
        j_.trace(),
        "chain listener message",
        jv("msg", msg),
        jv("isMainchain", isMainchain_));

    if (!msg.isMember(jss::validated) || !msg[jss::validated].asBool())
    {
        JLOGV(
            j_.trace(),
            "ignoring listener message",
            jv("reason", "not validated"),
            jv("msg", msg),
            jv("chain_name", chainName()));
        return;
    }

    if (!msg.isMember(jss::engine_result_code))
    {
        JLOGV(
            j_.trace(),
            "ignoring listener message",
            jv("reason", "no engine result code"),
            jv("msg", msg),
            jv("chain_name", chainName()));
        return;
    }

    if (!msg.isMember(jss::account_history_tx_index))
    {
        JLOGV(
            j_.trace(),
            "ignoring listener message",
            jv("reason", "no account history tx index"),
            jv("msg", msg),
            jv("chain_name", chainName()));
        return;
    }

    if (!msg.isMember(jss::meta))
    {
        JLOGV(
            j_.trace(),
            "ignoring listener message",
            jv("reason", "tx meta"),
            jv("msg", msg),
            jv("chain_name", chainName()));
        return;
    }

    auto fieldMatchesStr =
        [](Json::Value const& val, char const* field, char const* toMatch) {
            if (!val.isMember(field))
                return false;
            auto const f = val[field];
            if (!f.isString())
                return false;
            return f.asString() == toMatch;
        };

    TER const txnTER = [&msg] {
        return TER::fromInt(msg[jss::engine_result_code].asInt());
    }();

    bool const txnSuccess = (txnTER == tesSUCCESS);

    // values < 0 are historical txns. values >= 0 are new transactions. Only
    // the initial sync needs historical txns.
    int const txnHistoryIndex = msg[jss::account_history_tx_index].asInt();

    auto const meta = msg[jss::meta];

    // There are two payment types of interest:
    // 1. User initiated payments on this chain that trigger a transaction on
    // the other chain.
    // 2. Federated initated payments on this chain whose status needs to be
    // checked.
    enum class PaymentType { user, federator };
    auto paymentTypeOpt = [&]() -> std::optional<PaymentType> {
        // Only keep transactions to or from the door account.
        // Transactions to the account are initiated by users and are are cross
        // chain transactions. Transaction from the account are initiated by
        // federators and need to be monitored for errors. There are two types
        // of transactions that originate from the door account: the second half
        // of a cross chain payment and a refund of a failed cross chain
        // payment.

        if (!fieldMatchesStr(msg, jss::type, jss::transaction))
            return {};

        if (!msg.isMember(jss::transaction))
            return {};
        auto const txn = msg[jss::transaction];

        if (!fieldMatchesStr(txn, jss::TransactionType, "Payment"))
            return {};

        bool const accIsSrc =
            fieldMatchesStr(txn, jss::Account, doorAccountStr_.c_str());
        bool const accIsDst =
            fieldMatchesStr(txn, jss::Destination, doorAccountStr_.c_str());

        if (accIsSrc == accIsDst)
        {
            // either account is not involved, or self send
            return {};
        }

        if (accIsSrc)
            return PaymentType::federator;
        return PaymentType::user;
    }();

    // There are four types of messages used to control the federator accounts:
    // 1. AccountSet without modifying account settings. These txns are used to
    // trigger TicketCreate txns.
    // 2. TicketCreate to issue tickets.
    // 3. AccountSet that changes the depositAuth setting of accounts.
    // 4. SignerListSet to update the signerList of accounts.
    // 5. AccoutSet that disables the master key. All transactions before this
    // are used for setup only and should be ignored. This transaction is also
    // used to help set the initial transaction sequence numbers
    enum class AccountControlType {
        trigger,
        ticket,
        depositAuth,
        signerList,
        disableMasterKey
    };
    auto accountControlTypeOpt = [&]() -> std::optional<AccountControlType> {
        if (!fieldMatchesStr(msg, jss::type, jss::transaction))
            return {};

        if (!msg.isMember(jss::transaction))
            return {};
        auto const txn = msg[jss::transaction];

        if (fieldMatchesStr(txn, jss::TransactionType, "AccountSet"))
        {
            if (!(txn.isMember(jss::SetFlag) || txn.isMember(jss::ClearFlag)))
            {
                return AccountControlType::trigger;
            }
            else
            {
                // Get the flags value at the key. If the key is not present,
                // return 0.
                auto getFlags =
                    [&txn](Json::StaticString const& key) -> std::uint32_t {
                    if (txn.isMember(key))
                    {
                        auto const val = txn[key];
                        try
                        {
                            return val.asUInt();
                        }
                        catch (...)
                        {
                        }
                    }
                    return 0;
                };

                std::uint32_t const setFlags = getFlags(jss::SetFlag);
                std::uint32_t const clearFlags = getFlags(jss::ClearFlag);

                if (setFlags == asfDepositAuth || clearFlags == asfDepositAuth)
                    return AccountControlType::depositAuth;

                if (setFlags == asfDisableMaster)
                    return AccountControlType::disableMasterKey;
            }
        }
        if (fieldMatchesStr(txn, jss::TransactionType, "TicketCreate"))
            return AccountControlType::ticket;
        if (fieldMatchesStr(txn, jss::TransactionType, "SignerListSet"))
            return AccountControlType::signerList;

        return {};
    }();

    if (!paymentTypeOpt && !accountControlTypeOpt)
    {
        JLOGV(
            j_.warn(),
            "ignoring listener message",
            jv("reason", "wrong type, not payment nor account control tx"),
            jv("msg", msg),
            jv("chain_name", chainName()));
        return;
    }
    assert(!paymentTypeOpt || !accountControlTypeOpt);

    auto const txnHash = [&]() -> std::optional<uint256> {
        try
        {
            uint256 result;
            if (result.parseHex(msg[jss::transaction][jss::hash].asString()))
                return result;
        }
        catch (...)
        {
        }
        // TODO: this is an insane input stream
        // Detect and connect to another server
        return {};
    }();
    if (!txnHash)
    {
        JLOG(j_.warn()) << "ignoring listener message, no tx hash";
        return;
    }

    auto const seq = [&]() -> std::optional<std::uint32_t> {
        try
        {
            return msg[jss::transaction][jss::Sequence].asUInt();
        }
        catch (...)
        {
            // TODO: this is an insane input stream
            // Detect and connect to another server
            return {};
        }
    }();
    if (!seq)
    {
        JLOG(j_.warn()) << "ignoring listener message, no tx seq";
        return;
    }

    if (paymentTypeOpt)
    {
        PaymentType const paymentType = *paymentTypeOpt;

        std::optional<STAmount> deliveredAmt;
        if (meta.isMember(jss::delivered_amount))
        {
            deliveredAmt =
                amountFromJson(sfGeneric, meta[jss::delivered_amount]);
        }

        auto const src = [&]() -> std::optional<AccountID> {
            try
            {
                return parseBase58<AccountID>(
                    msg[jss::transaction][jss::Account].asString());
            }
            catch (...)
            {
            }
            // TODO: this is an insane input stream
            // Detect and connect to another server
            return {};
        }();
        if (!src)
        {
            // TODO: handle the error
            return;
        }

        auto const dst = [&]() -> std::optional<AccountID> {
            try
            {
                switch (paymentType)
                {
                    case PaymentType::user: {
                        // This is the destination of the "other chain"
                        // transfer, which is specified as a memo.
                        if (!msg.isMember(jss::transaction))
                        {
                            return std::nullopt;
                        }
                        try
                        {
                            // the memo data is a hex encoded version of the
                            // base58 encoded address. This was chosen for ease
                            // of encoding by clients.
                            auto const hexData =
                                msg[jss::transaction][jss::Memos][0u][jss::Memo]
                                   [jss::MemoData]
                                       .asString();
                            if ((hexData.size() > 100) || (hexData.size() % 2))
                                return std::nullopt;

                            auto const asciiData = [&]() -> std::string {
                                std::string result;
                                result.reserve(40);
                                auto d = hexData.data();
                                for (int i = 0; i < hexData.size(); i += 2)
                                {
                                    auto highNibble = charUnHex(d[i]);
                                    auto lowNibble = charUnHex(d[i + 1]);
                                    if (highNibble < 0 || lowNibble < 0)
                                        return {};
                                    char c = (highNibble << 4) | lowNibble;
                                    result.push_back(c);
                                }
                                return result;
                            }();
                            return parseBase58<AccountID>(asciiData);
                        }
                        catch (...)
                        {
                            // User did not specify a destination address in a
                            // memo
                            return std::nullopt;
                        }
                    }
                    case PaymentType::federator:
                        return parseBase58<AccountID>(
                            msg[jss::transaction][jss::Destination].asString());
                }
            }
            catch (...)
            {
            }
            // TODO: this is an insane input stream
            // Detect and connect to another server
            return {};
        }();
        if (!dst)
        {
            // TODO: handle the error
            return;
        }

        switch (paymentType)
        {
            case PaymentType::federator: {
                auto s = txnSuccess ? j_.trace() : j_.error();
                char const* status = txnSuccess ? "success" : "fail";
                JLOGV(
                    s,
                    "federator txn status",
                    jv("chain_name", chainName()),
                    jv("status", status),
                    jv("msg", msg));

                auto const txnTypeRaw =
                    detail::getMemoData<uint8_t>(msg[jss::transaction], 0);

                if (!txnTypeRaw || *txnTypeRaw > Federator::txnTypeLast)
                {
                    JLOGV(
                        j_.fatal(),
                        "expected valid txnType in ChainListener",
                        jv("msg", msg));
                    return;
                }

                Federator::TxnType const txnType =
                    static_cast<Federator::TxnType>(*txnTypeRaw);

                auto const srcChainTxnHash =
                    detail::getMemoData<uint256>(msg[jss::transaction], 1);

                if (!srcChainTxnHash)
                {
                    JLOGV(
                        j_.fatal(),
                        "expected srcChainTxnHash in ChainListener",
                        jv("msg", msg));
                    return;
                }
                static_assert(
                    Federator::txnTypeLast == 2, "Add new case below");
                switch (txnType)
                {
                    case Federator::TxnType::xChain: {
                        using namespace event;
                        // The dirction looks backwards, but it's not. The
                        // direction is for the *triggering* transaction.
                        auto const dir =
                            isMainchain_ ? Dir::sideToMain : Dir::mainToSide;
                        XChainTransferResult e{
                            dir,
                            *dst,
                            deliveredAmt,
                            *seq,
                            *srcChainTxnHash,
                            *txnHash,
                            txnTER,
                            txnHistoryIndex};
                        pushEvent(std::move(e), txnHistoryIndex, l);
                    }
                    break;
                    case Federator::TxnType::refund: {
                        using namespace event;
                        // The direction is for the triggering transaction.
                        auto const dir =
                            isMainchain_ ? Dir::mainToSide : Dir::sideToMain;
                        auto const dstChainTxnHash =
                            detail::getMemoData<uint256>(
                                msg[jss::transaction], 2);
                        if (!dstChainTxnHash)
                        {
                            JLOGV(
                                j_.fatal(),
                                "expected valid dstChainTxnHash in "
                                "ChainListener",
                                jv("msg", msg));
                            return;
                        }
                        RefundTransferResult e{
                            dir,
                            *dst,
                            deliveredAmt,
                            *seq,
                            *srcChainTxnHash,
                            *dstChainTxnHash,
                            *txnHash,
                            txnTER,
                            txnHistoryIndex};
                        pushEvent(std::move(e), txnHistoryIndex, l);
                    }
                    break;
                }
            }
            break;
            case PaymentType::user: {
                if (!txnSuccess)
                    return;

                if (!deliveredAmt)
                    return;
                {
                    using namespace event;
                    XChainTransferDetected e{
                        isMainchain_ ? Dir::mainToSide : Dir::sideToMain,
                        *src,
                        *dst,
                        *deliveredAmt,
                        *seq,
                        *txnHash,
                        txnHistoryIndex};
                    pushEvent(std::move(e), txnHistoryIndex, l);
                }
            }
            break;
        }
    }
    else
    {
        // account control tx
        auto const ledgerIndex = [&]() -> std::optional<std::uint32_t> {
            try
            {
                return msg["ledger_index"].asInt();
            }
            catch (...)
            {
                JLOGV(j_.error(), "no ledger_index", jv("message", msg));
                assert(false);
                return {};
            }
        }();
        if (!ledgerIndex)
        {
            JLOG(j_.warn()) << "ignoring listener message, no ledgerIndex";
            return;
        }

        auto const getSourceTag = [&]() -> std::optional<std::uint32_t> {
            try
            {
                return msg[jss::transaction]["SourceTag"].asUInt();
            }
            catch (...)
            {
                JLOGV(j_.error(), "wrong SourceTag", jv("message", msg));
                assert(false);
                return {};
            }
        };

        auto const getMemoStr = [&](std::uint32_t index) -> std::string {
            try
            {
                if (msg[jss::transaction][jss::Memos][index] ==
                    Json::Value::null)
                    return {};
                auto str = std::string(msg[jss::transaction][jss::Memos][index]
                                          [jss::Memo][jss::MemoData]
                                              .asString());
                assert(str.length() <= event::MemoStringMax);
                return str;
            }
            catch (...)
            {
            }
            return {};
        };

        auto const accountControlType = *accountControlTypeOpt;
        switch (accountControlType)
        {
            case AccountControlType::trigger: {
                JLOGV(
                    j_.trace(),
                    "AccountControlType::trigger",
                    jv("chain_name", chainName()),
                    jv("account_seq", *seq),
                    jv("msg", msg));
                auto sourceTag = getSourceTag();
                if (!sourceTag)
                {
                    JLOG(j_.warn())
                        << "ignoring listener message, no sourceTag";
                    return;
                }
                auto memoStr = getMemoStr(0);
                event::TicketCreateTrigger e = {
                    isMainchain_ ? event::Dir::mainToSide
                                 : event::Dir::sideToMain,
                    txnSuccess,
                    0,
                    *ledgerIndex,
                    *txnHash,
                    txnHistoryIndex,
                    *sourceTag,
                    std::move(memoStr)};
                pushEvent(std::move(e), txnHistoryIndex, l);
                break;
            }
            case AccountControlType::ticket: {
                JLOGV(
                    j_.trace(),
                    "AccountControlType::ticket",
                    jv("chain_name", chainName()),
                    jv("account_seq", *seq),
                    jv("msg", msg));
                auto sourceTag = getSourceTag();
                if (!sourceTag)
                {
                    JLOG(j_.warn())
                        << "ignoring listener message, no sourceTag";
                    return;
                }

                auto const triggeringTxnHash =
                    detail::getMemoData<uint256>(msg[jss::transaction], 0);
                if (!triggeringTxnHash)
                {
                    JLOGV(
                        (txnSuccess ? j_.trace() : j_.error()),
                        "bootstrap ticket",
                        jv("chain_name", chainName()),
                        jv("account_seq", *seq),
                        jv("msg", msg));

                    if (!txnSuccess)
                        return;

                    event::BootstrapTicket e = {
                        isMainchain_,
                        txnSuccess,
                        *seq,
                        *ledgerIndex,
                        txnHistoryIndex,
                        *sourceTag};
                    pushEvent(std::move(e), txnHistoryIndex, l);
                    return;
                }

                // The TicketCreate tx is both the result of its triggering
                // AccountSet tx, and the trigger of another account control tx,
                // if there is a tx in the memo field.
                event::TicketCreateResult e = {
                    isMainchain_ ? event::Dir::sideToMain
                                 : event::Dir::mainToSide,
                    txnSuccess,
                    *seq,
                    *ledgerIndex,
                    *triggeringTxnHash,
                    *txnHash,
                    txnHistoryIndex,
                    *sourceTag,
                    getMemoStr(1)};
                pushEvent(std::move(e), txnHistoryIndex, l);
                break;
            }
            case AccountControlType::depositAuth: {
                JLOGV(
                    j_.trace(),
                    "AccountControlType::depositAuth",
                    jv("chain_name", chainName()),
                    jv("account_seq", *seq),
                    jv("msg", msg));
                auto const triggeringTxHash =
                    detail::getMemoData<uint256>(msg[jss::transaction], 0);
                if (!triggeringTxHash)
                {
                    JLOG(j_.warn())
                        << "ignoring listener message, no triggeringTxHash";
                    return;
                }

                auto opOpt = [&]() -> std::optional<event::AccountFlagOp> {
                    try
                    {
                        if (msg[jss::transaction].isMember(jss::SetFlag) &&
                            msg[jss::transaction][jss::SetFlag].isIntegral())
                        {
                            assert(
                                msg[jss::transaction][jss::SetFlag].asUInt() ==
                                asfDepositAuth);
                            return event::AccountFlagOp::set;
                        }
                        if (msg[jss::transaction].isMember(jss::ClearFlag) &&
                            msg[jss::transaction][jss::ClearFlag].isIntegral())
                        {
                            assert(
                                msg[jss::transaction][jss::ClearFlag]
                                    .asUInt() == asfDepositAuth);

                            return event::AccountFlagOp::clear;
                        }
                    }
                    catch (...)
                    {
                    }
                    JLOGV(
                        j_.error(),
                        "unexpected accountSet tx",
                        jv("message", msg));
                    assert(false);
                    return {};
                }();
                if (!opOpt)
                    return;

                event::DepositAuthResult e{
                    isMainchain_ ? event::Dir::sideToMain
                                 : event::Dir::mainToSide,
                    txnSuccess,
                    *seq,
                    *ledgerIndex,
                    *triggeringTxHash,
                    txnHistoryIndex,
                    *opOpt};
                pushEvent(std::move(e), txnHistoryIndex, l);
                break;
            }
            case AccountControlType::signerList:
                // TODO
                break;
            case AccountControlType::disableMasterKey: {
                event::DisableMasterKeyResult e{
                    isMainchain_, *seq, txnHistoryIndex};
                pushEvent(std::move(e), txnHistoryIndex, l);
                break;
            }
            break;
        }
    }

    // Note: Handling "last in history" is done through the lambda given
    // to `make_scope` earlier in the function
}

void
ChainListener::setLastXChainTxnWithResult(uint256 const& hash)
{
    // Note that `onMessage` also locks this mutex, and it calls
    // `setLastXChainTxnWithResult`. However, it calls that function on the
    // other chain, so the mutex will not be locked twice on the same
    // thread.
    std::lock_guard l{m_};
    if (!initialSync_)
        return;

    auto const hasReplayed = initialSync_->setLastXChainTxnWithResult(hash);
    if (hasReplayed)
        initialSync_.reset();
}

void
ChainListener::setNoLastXChainTxnWithResult()
{
    // Note that `onMessage` also locks this mutex, and it calls
    // `setNoLastXChainTxnWithResult`. However, it calls that function on
    // the other chain, so the mutex will not be locked twice on the same
    // thread.
    std::lock_guard l{m_};
    if (!initialSync_)
        return;

    bool const hasReplayed = initialSync_->setNoLastXChainTxnWithResult();
    if (hasReplayed)
        initialSync_.reset();
}

Json::Value
ChainListener::getInfo() const
{
    std::lock_guard l{m_};

    Json::Value ret{Json::objectValue};
    ret[jss::state] = initialSync_ ? "syncing" : "normal";
    if (initialSync_)
    {
        ret[jss::sync_info] = initialSync_->getInfo();
    }
    // get the state (in sync, syncing)
    return ret;
}

}  // namespace sidechain
}  // namespace ripple
