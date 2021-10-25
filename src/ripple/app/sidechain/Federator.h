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

#ifndef RIPPLE_SIDECHAIN_FEDERATOR_H_INCLUDED
#define RIPPLE_SIDECHAIN_FEDERATOR_H_INCLUDED

#include <ripple/app/sidechain/FederatorEvents.h>
#include <ripple/app/sidechain/impl/DoorKeeper.h>
#include <ripple/app/sidechain/impl/MainchainListener.h>
#include <ripple/app/sidechain/impl/SidechainListener.h>
#include <ripple/app/sidechain/impl/SignatureCollector.h>
#include <ripple/app/sidechain/impl/SignerList.h>
#include <ripple/app/sidechain/impl/TicketHolder.h>
#include <ripple/basics/Buffer.h>
#include <ripple/basics/ThreadSaftyAnalysis.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/Config.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/SecretKey.h>

#include <boost/container/flat_map.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ripple {

class Application;
class STTx;

namespace sidechain {

class Federator : public std::enable_shared_from_this<Federator>
{
public:
    enum ChainType { sideChain, mainChain };
    constexpr static size_t numChains = 2;

    enum class UnlockMainLoopKey { app, mainChain, sideChain };
    constexpr static size_t numUnlockMainLoopKeys = 3;

    // These enums are encoded in the transaction. Changing the order will break
    // backward compatibility. If a new type is added change txnTypeLast.
    enum class TxnType { xChain, refund };
    constexpr static std::uint8_t txnTypeLast = 2;

    static constexpr std::uint32_t accountControlTxFee{1000};

private:
    // Tag so make_Federator can call `std::make_shared`
    class PrivateTag
    {
    };

    friend std::shared_ptr<Federator>
    make_Federator(
        Application& app,
        boost::asio::io_service& ios,
        BasicConfig const& config,
        beast::Journal j);

    std::thread thread_;
    bool running_ = false;
    std::atomic<bool> requestStop_ = false;

    Application& app_;
    std::array<AccountID, numChains> const account_;
    std::array<std::atomic<std::uint32_t>, numChains> accountSeq_{1, 1};
    std::array<std::atomic<std::uint32_t>, numChains> lastTxnSeqSent_{0, 0};
    std::array<std::atomic<std::uint32_t>, numChains> lastTxnSeqConfirmed_{
        0,
        0};
    std::array<std::atomic<bool>, numUnlockMainLoopKeys> unlockMainLoopKeys_{
        false,
        false,
        false};
    std::shared_ptr<MainchainListener> mainchainListener_;
    std::shared_ptr<SidechainListener> sidechainListener_;

    mutable std::mutex eventsMutex_;
    std::vector<FederatorEvent> GUARDED_BY(eventsMutex_) events_;

    // When a user account sends an asset to the account controlled by the
    // federator, the asset to be issued on the other chain is determined by the
    // `assetProps` maps - one for each chain. The asset to be issued is
    // `issue`, the amount of the asset to issue is determined by `quality`
    // (ratio of output amount/input amount). When issuing refunds, the
    // `refundPenalty` is subtracted from the sent amount before sending the
    // refund.
    struct OtherChainAssetProperties
    {
        Quality quality;
        Issue issue;
        STAmount refundPenalty;
    };

    std::array<
        boost::container::flat_map<Issue, OtherChainAssetProperties>,
        numChains> const assetProps_;

    PublicKey signingPK_;
    SecretKey signingSK_;

    // federator signing public keys
    mutable std::mutex federatorPKsMutex_;
    hash_set<PublicKey> GUARDED_BY(federatorPKsMutex_) federatorPKs_;

    SignerList mainSignerList_;
    SignerList sideSignerList_;
    SignatureCollector mainSigCollector_;
    SignatureCollector sideSigCollector_;
    TicketRunner ticketRunner_;
    DoorKeeper mainDoorKeeper_;
    DoorKeeper sideDoorKeeper_;

    struct PeerTxnSignature
    {
        Buffer sig;
        std::uint32_t seq;
    };

    struct SequenceInfo
    {
        // Number of signatures at this sequence number
        std::uint32_t count{0};
        // Serialization of the transaction for everything except the signature
        // id (which varies for each signature). This can be used to verify one
        // of the signatures in a multisig.
        Blob partialTxnSerialization;
    };

    struct PendingTransaction
    {
        STAmount amount;
        AccountID srcChainSrcAccount;
        AccountID dstChainDstAccount;
        // Key is the federator's public key
        hash_map<PublicKey, PeerTxnSignature> sigs;
        // Key is a sequence number
        hash_map<std::uint32_t, SequenceInfo> sequenceInfo;
        // True if the transaction was ever put into the toSendTxns_ queue
        bool queuedToSend_{false};
    };

    // Key is the hash of the triggering transaction
    mutable std::mutex pendingTxnsM_;
    hash_map<uint256, PendingTransaction> GUARDED_BY(pendingTxnsM_)
        pendingTxns_[numChains];

    mutable std::mutex toSendTxnsM_;
    // Signed transactions ready to send
    // Key is the transaction's sequence number. The transactions must be sent
    // in the correct order. If the next trasnaction the account needs to send
    // has a sequence number of N, the transaction with sequence N+1 can't be
    // sent just because it collected signatures first.
    std::map<std::uint32_t, STTx> GUARDED_BY(toSendTxnsM_)
        toSendTxns_[numChains];
    std::set<std::uint32_t> GUARDED_BY(toSendTxnsM_) toSkipSeq_[numChains];

    // Use a condition variable to prevent busy waiting when the queue is
    // empty
    mutable std::mutex m_;
    std::condition_variable cv_;

    // prevent the main loop from starting until explictly told to run.
    // This is used to allow bootstrap code to run before any events are
    // processed
    mutable std::mutex mainLoopMutex_;
    bool mainLoopLocked_{true};
    std::condition_variable mainLoopCv_;
    beast::Journal j_;

    static std::array<
        boost::container::flat_map<Issue, OtherChainAssetProperties>,
        numChains>
    makeAssetProps(BasicConfig const& config, beast::Journal j);

public:
    // Constructor should be private, but needs to be public so
    // `make_shared` can use it
    Federator(
        PrivateTag,
        Application& app,
        SecretKey signingKey,
        hash_set<PublicKey>&& federators,
        boost::asio::ip::address mainChainIp,
        std::uint16_t mainChainPort,
        AccountID const& mainAccount,
        AccountID const& sideAccount,
        std::array<
            boost::container::flat_map<Issue, OtherChainAssetProperties>,
            numChains>&& assetProps,
        beast::Journal j);

    ~Federator();

    void
    start();

    void
    stop() EXCLUDES(m_);

    void
    push(FederatorEvent&& e) EXCLUDES(m_, eventsMutex_);

    // Don't process any events until the bootstrap has a chance to run
    void
    unlockMainLoop(UnlockMainLoopKey key) EXCLUDES(m_);

    void
    addPendingTxnSig(
        TxnType txnType,
        ChainType chaintype,
        PublicKey const& federatorPk,
        uint256 const& srcChainTxnHash,
        std::optional<uint256> const& dstChainTxnHash,
        STAmount const& amt,
        AccountID const& srcChainSrcAccount,
        AccountID const& dstChainDstAccount,
        std::uint32_t seq,
        Buffer&& sig) EXCLUDES(federatorPKsMutex_, pendingTxnsM_, toSendTxnsM_);

    void
    addPendingTxnSig(
        ChainType chaintype,
        PublicKey const& publicKey,
        uint256 const& mId,
        Buffer&& sig);

    // Return true if a transaction with this sequence has already been sent
    bool
    alreadySent(ChainType chaintype, std::uint32_t seq) const;

    void
    setLastXChainTxnWithResult(
        ChainType chaintype,
        std::uint32_t seq,
        std::uint32_t seqTook,
        uint256 const& hash);
    void
    setNoLastXChainTxnWithResult(ChainType chaintype);
    void
    stopHistoricalTxns(ChainType chaintype);
    void
    initialSyncDone(ChainType chaintype);

    // Get stats on the federator, including pending transactions and
    // initialization state
    Json::Value
    getInfo() const EXCLUDES(pendingTxnsM_);

    void
    sweep();

    SignatureCollector&
    getSignatureCollector(ChainType chain);
    DoorKeeper&
    getDoorKeeper(ChainType chain);
    TicketRunner&
    getTicketRunner();
    void
    addSeqToSkip(ChainType chain, std::uint32_t seq) EXCLUDES(toSendTxnsM_);
    // TODO multi-sig refactor?
    void
    addTxToSend(ChainType chain, std::uint32_t seq, STTx const& tx)
        EXCLUDES(toSendTxnsM_);

    // Set the accountSeq to the max of the current value and the requested
    // value. This is done with a lock free algorithm.
    void
    setAccountSeqMax(ChainType chaintype, std::uint32_t reqValue);

private:
    // Two phase init needed for shared_from this.
    // Only called from `make_Federator`
    void
    init(
        boost::asio::io_service& ios,
        boost::asio::ip::address& ip,
        std::uint16_t port,
        std::shared_ptr<MainchainListener>&& mainchainListener,
        std::shared_ptr<SidechainListener>&& sidechainListener);

    // Convert between the asset on the src chain to the asset on the other
    // chain. The `assetProps_` array controls how this conversion is done.
    // An empty option is returned if the from issue is not part of the map in
    // the `assetProps_` array.
    [[nodiscard]] std::optional<STAmount>
    toOtherChainAmount(ChainType srcChain, STAmount const& from) const;

    // Set the lastTxnSeqSent to the max of the current value and the requested
    // value. This is done with a lock free algorithm.
    void
    setLastTxnSeqSentMax(ChainType chaintype, std::uint32_t reqValue);
    void
    setLastTxnSeqConfirmedMax(ChainType chaintype, std::uint32_t reqValue);

    mutable std::mutex sendTxnsMutex_;
    void
    sendTxns() EXCLUDES(sendTxnsMutex_, toSendTxnsM_);

    void
    mainLoop() EXCLUDES(mainLoopMutex_);

    void
    payTxn(
        TxnType txnType,
        ChainType dstChain,
        STAmount const& amt,
        // srcChainSrcAccount is the origional sending account in a cross chain
        // transaction. Note, for refunds, the srcChainSrcAccount and the dst
        // will be the same.
        AccountID const& srcChainSrcAccount,
        AccountID const& dst,
        uint256 const& srcChainTxnHash,
        std::optional<uint256> const& dstChainTxnHash);

    // Issue a refund to the destination account. Refunds may be issued when a
    // cross chain transaction fails on the destination chain. In this case, the
    // funds will already be locked on one chain, but can not be completed on
    // the other chain. Note that refunds may not be for the full amount sent.
    // In effect, not refunding the full amount charges a fee to discourage
    // abusing refunds to try to overload the system.
    void
    sendRefund(
        ChainType chaintype,
        STAmount const& amt,
        AccountID const& dst,
        uint256 const& txnHash,
        uint256 const& triggeringResultTxnHash);

    void
    onEvent(event::XChainTransferDetected const& e);
    void
    onEvent(event::XChainTransferResult const& e) EXCLUDES(pendingTxnsM_);
    void
    onEvent(event::RefundTransferResult const& e) EXCLUDES(pendingTxnsM_);
    void
    onEvent(event::HeartbeatTimer const& e);
    void
    onEvent(event::TicketCreateTrigger const& e);
    void
    onEvent(event::TicketCreateResult const& e);
    void
    onEvent(event::DepositAuthResult const& e);
    void
    onEvent(event::BootstrapTicket const& e);
    void
    onEvent(event::DisableMasterKeyResult const& e);
    //    void
    //    onEvent(event::SignerListSetResult const& e);

    void
    updateDoorKeeper(ChainType chainType) EXCLUDES(pendingTxnsM_);
    void
    onResult(ChainType chainType, std::uint32_t resultTxSeq);
};

[[nodiscard]] std::shared_ptr<Federator>
make_Federator(
    Application& app,
    boost::asio::io_service& ios,
    BasicConfig const& config,
    beast::Journal j);

// Id used for message suppression
[[nodiscard]] uint256
crossChainTxnSignatureId(
    PublicKey signingPK,
    uint256 const& srcChainTxnHash,
    std::optional<uint256> const& dstChainTxnHash,
    STAmount const& amt,
    AccountID const& src,
    AccountID const& dst,
    std::uint32_t seq,
    Slice const& signature);

[[nodiscard]] Federator::ChainType
srcChainType(event::Dir dir);

[[nodiscard]] Federator::ChainType
dstChainType(event::Dir dir);

[[nodiscard]] Federator::ChainType
otherChainType(Federator::ChainType ct);

[[nodiscard]] Federator::ChainType
getChainType(bool isMainchain);

uint256
computeMessageSuppression(uint256 const& mId, Slice const& signature);
}  // namespace sidechain
}  // namespace ripple

#endif
