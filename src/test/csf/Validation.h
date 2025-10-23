#ifndef XRPL_TEST_CSF_VALIDATION_H_INCLUDED
#define XRPL_TEST_CSF_VALIDATION_H_INCLUDED

#include <test/csf/ledgers.h>

#include <xrpl/basics/tagged_integer.h>

#include <memory>
#include <optional>
#include <utility>

namespace ripple {
namespace test {
namespace csf {

struct PeerIDTag;
//< Uniquely identifies a peer
using PeerID = tagged_integer<std::uint32_t, PeerIDTag>;

/** The current key of a peer

    Eventually, the second entry in the pair can be used to model ephemeral
    keys. Right now, the convention is to have the second entry 0 as the
    master key.
*/
using PeerKey = std::pair<PeerID, std::uint32_t>;

/** Validation of a specific ledger by a specific Peer.
 */
class Validation
{
    Ledger::ID ledgerID_{0};
    Ledger::Seq seq_{0};

    NetClock::time_point signTime_;
    NetClock::time_point seenTime_;
    PeerKey key_;
    PeerID nodeID_{0};
    bool trusted_ = false;
    bool full_ = false;
    std::optional<std::uint32_t> loadFee_;
    std::uint64_t cookie_{0};

public:
    using NodeKey = PeerKey;
    using NodeID = PeerID;

    Validation(
        Ledger::ID id,
        Ledger::Seq seq,
        NetClock::time_point sign,
        NetClock::time_point seen,
        PeerKey key,
        PeerID nodeID,
        bool full,
        std::optional<std::uint32_t> loadFee = std::nullopt,
        std::uint64_t cookie = 0)
        : ledgerID_{id}
        , seq_{seq}
        , signTime_{sign}
        , seenTime_{seen}
        , key_{key}
        , nodeID_{nodeID}
        , full_{full}
        , loadFee_{loadFee}
        , cookie_{cookie}
    {
    }

    Ledger::ID
    ledgerID() const
    {
        return ledgerID_;
    }

    Ledger::Seq
    seq() const
    {
        return seq_;
    }

    NetClock::time_point
    signTime() const
    {
        return signTime_;
    }

    NetClock::time_point
    seenTime() const
    {
        return seenTime_;
    }

    PeerKey const&
    key() const
    {
        return key_;
    }

    PeerID const&
    nodeID() const
    {
        return nodeID_;
    }

    bool
    trusted() const
    {
        return trusted_;
    }

    bool
    full() const
    {
        return full_;
    }

    std::uint64_t
    cookie() const
    {
        return cookie_;
    }

    std::optional<std::uint32_t>
    loadFee() const
    {
        return loadFee_;
    }

    Validation const&
    unwrap() const
    {
        // For the rippled implementation in which RCLValidation wraps
        // STValidation, the csf::Validation has no more specific type it
        // wraps, so csf::Validation unwraps to itself
        return *this;
    }

    auto
    asTie() const
    {
        // trusted is a status set by the receiver, so it is not part of the tie
        return std::tie(
            ledgerID_,
            seq_,
            signTime_,
            seenTime_,
            key_,
            nodeID_,
            loadFee_,
            full_);
    }
    bool
    operator==(Validation const& o) const
    {
        return asTie() == o.asTie();
    }

    bool
    operator<(Validation const& o) const
    {
        return asTie() < o.asTie();
    }

    void
    setTrusted()
    {
        trusted_ = true;
    }

    void
    setUntrusted()
    {
        trusted_ = false;
    }

    void
    setSeen(NetClock::time_point seen)
    {
        seenTime_ = seen;
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
