#pragma once

#include <xrpld/app/consensus/RCLCxPeerPos.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STValidation.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/Manifest.h>
#include <xrpl/server/NetworkOPs.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace xrpl::test {

/**
 * A `NetworkOPs` whose every method is a no-op returning a default.
 *
 * It exists so a handler test can supply its own operations object through
 * `rpc::Context::netOps`: derive from this and override only the methods the
 * test cares about. Adding a method to `NetworkOPs` or `InfoSub::Source` then
 * costs one stub here, not one per double.
 */
class NetworkOPsStub : public NetworkOPs
{
public:
    NetworkOPsStub() = default;
    ~NetworkOPsStub() override = default;

    void
    stop() override
    {
    }

    [[nodiscard]] OperatingMode
    getOperatingMode() const override
    {
        return {};
    }

    [[nodiscard]] std::string
    strOperatingMode(OperatingMode const, bool const) const override
    {
        return {};
    }

    [[nodiscard]] std::string
    strOperatingMode(bool const) const override
    {
        return {};
    }

    void
    submitTransaction(std::shared_ptr<STTx const> const&) override
    {
    }

    void
    processTransaction(std::shared_ptr<Transaction>&, bool, bool, FailHard) override
    {
    }

    void
    processTransactionSet(CanonicalTXSet const&) override
    {
    }

    json::Value
    getOwnerInfo(std::shared_ptr<ReadView const>, AccountID const&) override
    {
        return {};
    }

    void
    getBookPage(
        std::shared_ptr<ReadView const>&,
        Book const&,
        AccountID const&,
        bool const,
        unsigned int,
        json::Value const&,
        json::Value&) override
    {
    }

    bool
    processTrustedProposal(RCLCxPeerPos) override
    {
        return {};
    }

    bool
    recvValidation(std::shared_ptr<STValidation> const&, std::string const&) override
    {
        return {};
    }

    void
    mapComplete(std::shared_ptr<SHAMap> const&, bool) override
    {
    }

    bool
    beginConsensus(UInt256 const&, std::unique_ptr<std::stringstream> const&) override
    {
        return {};
    }

    void
    endConsensus(std::unique_ptr<std::stringstream> const&) override
    {
    }

    void
    setStandAlone() override
    {
    }

    void
    setStateTimer() override
    {
    }

    void
    setNeedNetworkLedger() override
    {
    }

    void
    clearNeedNetworkLedger() override
    {
    }

    bool
    isNeedNetworkLedger() override
    {
        return {};
    }

    bool
    isFull() override
    {
        return {};
    }

    void
    setMode(OperatingMode) override
    {
    }

    bool
    isBlocked() override
    {
        return {};
    }

    bool
    isAmendmentBlocked() override
    {
        return {};
    }

    void
    setAmendmentBlocked() override
    {
    }

    bool
    isAmendmentWarned() override
    {
        return {};
    }

    void
    setAmendmentWarned() override
    {
    }

    void
    clearAmendmentWarned() override
    {
    }

    bool
    isUNLBlocked() override
    {
        return {};
    }

    void
    setUNLBlocked() override
    {
    }

    void
    clearUNLBlocked() override
    {
    }

    void
    consensusViewChange() override
    {
    }

    json::Value
    getConsensusInfo() override
    {
        return {};
    }

    json::Value
    getServerInfo(bool, bool, bool) override
    {
        return {};
    }

    void
    clearLedgerFetch() override
    {
    }

    json::Value
    getLedgerFetchInfo() override
    {
        return {};
    }

    std::uint32_t
    acceptLedger(std::optional<std::chrono::milliseconds>) override
    {
        return {};
    }

    void
    reportFeeChange() override
    {
    }

    void
    updateLocalTx(ReadView const&) override
    {
    }

    std::size_t
    getLocalTxCount() override
    {
        return {};
    }

    void
    pubLedger(std::shared_ptr<ReadView const> const&) override
    {
    }

    void
    pubProposedTransaction(
        std::shared_ptr<ReadView const> const&,
        std::shared_ptr<STTx const> const&,
        TER) override
    {
    }

    void
    pubValidation(std::shared_ptr<STValidation> const&) override
    {
    }

    void
    stateAccounting(json::Value&) override
    {
    }

    std::size_t
    getBookSubscribersCount() override
    {
        return {};
    }

    void
    subAccount(InfoSub::Ref, HashSet<AccountID> const&, bool) override
    {
    }

    void
    unsubAccount(InfoSub::Ref, HashSet<AccountID> const&, bool) override
    {
    }

    void
    unsubAccountInternal(std::uint64_t, HashSet<AccountID> const&, bool) override
    {
    }

    ErrorCodeI
    subAccountHistory(InfoSub::Ref, AccountID const&) override
    {
        return {};
    }

    void
    unsubAccountHistory(InfoSub::Ref, AccountID const&, bool) override
    {
    }

    void
    unsubAccountHistoryInternal(std::uint64_t, AccountID const&, bool) override
    {
    }

    void
    unsubMPTInternal(std::uint64_t, MPTID const&) override
    {
    }

    void
    scheduleAccountCleanup(
        std::uint64_t,
        HashSet<AccountID>,
        HashSet<AccountID>,
        HashSet<AccountID>) override
    {
    }

    bool
    subLedger(InfoSub::Ref, json::Value&) override
    {
        return {};
    }

    bool
    unsubLedger(std::uint64_t) override
    {
        return {};
    }

    bool
    subBookChanges(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubBookChanges(std::uint64_t) override
    {
        return {};
    }

    bool
    subManifests(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubManifests(std::uint64_t) override
    {
        return {};
    }

    void
    pubManifest(Manifest const&) override
    {
    }

    bool
    subServer(InfoSub::Ref, json::Value&, bool) override
    {
        return {};
    }

    bool
    unsubServer(std::uint64_t) override
    {
        return {};
    }

    bool
    subBook(InfoSub::Ref, Book const&) override
    {
        return {};
    }

    bool
    unsubBook(InfoSub::Ref, Book const&) override
    {
        return {};
    }

    bool
    unsubBookInternal(std::uint64_t, Book const&) override
    {
        return {};
    }

    bool
    subTransactions(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubTransactions(std::uint64_t) override
    {
        return {};
    }

    bool
    subRTTransactions(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubRTTransactions(std::uint64_t) override
    {
        return {};
    }

    bool
    subValidations(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubValidations(std::uint64_t) override
    {
        return {};
    }

    bool
    subPeerStatus(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubPeerStatus(std::uint64_t) override
    {
        return {};
    }

    void
    pubPeerStatus(std::function<json::Value()> const&) override
    {
    }

    bool
    subConsensus(InfoSub::Ref) override
    {
        return {};
    }

    bool
    unsubConsensus(std::uint64_t) override
    {
        return {};
    }

    void
    subMPT(InfoSub::Ref, HashSet<MPTID> const&) override
    {
    }

    void
    unsubMPT(InfoSub::Ref, HashSet<MPTID> const&) override
    {
    }

    InfoSub::pointer
    findRpcSub(std::string const&) override
    {
        return {};
    }

    InfoSub::pointer
    addRpcSub(std::string const&, InfoSub::Ref) override
    {
        return {};
    }

    bool
    tryRemoveRpcSub(std::string const&) override
    {
        return {};
    }

    [[nodiscard]] beast::Journal const&
    journal() const override
    {
        return j_;
    }

private:
    beast::Journal j_{beast::Journal::getNullSink()};
};

}  // namespace xrpl::test
