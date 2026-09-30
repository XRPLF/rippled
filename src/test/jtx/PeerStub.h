#pragma once

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

/**
 * A `Peer` whose every method is a no-op returning a default.
 *
 * Derive from this and override only the methods a test cares about. Adding a
 * method to `Peer` then costs one stub here, not one per double.
 *
 * The id and the node public key are real, because the code under test routes
 * and deduplicates on both.
 */
class PeerStub : public Peer
{
public:
    /**
     * @param id  The connection id reported by `id()`.
     */
    explicit PeerStub(id_t id = 0)
        : id_(id), nodePublicKey_(derivePublicKey(KeyType::Ed25519, randomSecretKey()))
    {
    }

    ~PeerStub() override = default;

    void
    send(std::shared_ptr<Message> const&) override
    {
    }

    [[nodiscard]] beast::ip::Endpoint
    getRemoteAddress() const override
    {
        return {};
    }

    void
    sendTxQueue() override
    {
    }

    void
    addTxQueue(uint256 const&) override
    {
    }

    void
    removeTxQueue(uint256 const&) override
    {
    }

    void
    charge(resource::Charge const&, std::string const&) override
    {
    }

    [[nodiscard]] id_t
    id() const override
    {
        return id_;
    }

    [[nodiscard]] bool
    cluster() const override
    {
        return false;
    }

    [[nodiscard]] bool
    isHighLatency() const override
    {
        return false;
    }

    [[nodiscard]] int
    getScore(bool) const override
    {
        return 0;
    }

    [[nodiscard]] PublicKey const&
    getNodePublic() const override
    {
        return nodePublicKey_;
    }

    json::Value
    json() override
    {
        return {};
    }

    [[nodiscard]] bool
    supportsFeature(ProtocolFeature) const override
    {
        return false;
    }

    [[nodiscard]] std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }

    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }

    [[nodiscard]] std::string const&
    fingerprint() const override
    {
        return fingerprint_;
    }

    [[nodiscard]] uint256
    getClosedLedgerHash() const override
    {
        return {};
    }

    [[nodiscard]] bool
    hasLedger(uint256 const&, std::uint32_t) const override
    {
        return false;
    }

    void
    ledgerRange(std::uint32_t&, std::uint32_t&) const override
    {
    }

    [[nodiscard]] bool
    hasTxSet(uint256 const&) const override
    {
        return false;
    }

    void
    cycleStatus() override
    {
    }

    bool
    hasRange(std::uint32_t, std::uint32_t) override
    {
        return false;
    }

    [[nodiscard]] bool
    compressionEnabled() const override
    {
        return false;
    }

    [[nodiscard]] bool
    txReduceRelayEnabled() const override
    {
        return false;
    }

private:
    id_t const id_;
    PublicKey const nodePublicKey_;
    std::string const fingerprint_;
};

}  // namespace xrpl::test
