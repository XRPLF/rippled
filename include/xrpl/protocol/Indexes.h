#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/UintTypes.h>

#include <boost/endian/conversion.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <utility>

namespace xrpl {

class SeqProxy;

// Structured-key helpers: read/write the low 64 bits of a uint256 keylet
// in big-endian byte order. Used by every AMM keylet scheme that
// embeds an ordered subkey (tick index, bitmap-word index, bin ID) into
// the low 64 bits so SHAMap range walks visit entries in subkey order.
//
// We type-pun via std::memcpy rather than a reinterpret_cast through
// uint64_t* — the latter violates strict aliasing and has no alignment
// guarantee on base_uint's underlying byte storage. memcpy of an
// 8-byte value compiles to a single load/store on x86_64 / ARM64 under
// any optimization level, so the safer idiom is free at runtime.
inline void
setLow64BE(uint256& key, std::uint64_t value) noexcept
{
    auto const be = boost::endian::native_to_big(value);
    std::memcpy(key.end() - sizeof(std::uint64_t), &be, sizeof(std::uint64_t));
}

[[nodiscard]] inline std::uint64_t
getLow64BE(uint256 const& key) noexcept
{
    std::uint64_t be = 0;
    std::memcpy(&be, key.end() - sizeof(std::uint64_t), sizeof(std::uint64_t));
    return boost::endian::big_to_native(be);
}
/**
 * Keylet computation functions.
 *
 * Entries in the ledger are located using 256-bit locators. The locators are
 * calculated using a wide range of parameters specific to the entry whose
 * locator we are calculating (e.g. an account's locator is derived from the
 * account's address, whereas the locator for an offer is derived from the
 * account and the offer sequence.)
 *
 * To enhance type safety during lookup and make the code more robust, we use
 * keylets, which contain not only the locator of the object but also the type
 * of the object being referenced.
 *
 * These functions each return a type-specific keylet.
 */
namespace keylet {

/**
 * AccountID root
 */
Keylet
account(AccountID const& id) noexcept;

/**
 * The index of the amendment table
 */
Keylet const&
amendments() noexcept;

/**
 * Any item that can be in an owner dir.
 */
Keylet
child(UInt256 const& key) noexcept;

/**
 * The index of the "short" skip list
 *
 * The "short" skip list is a node (at a fixed index) that holds the hashes
 * of ledgers since the last flag ledger. It will contain, at most, 256 hashes.
 */
Keylet const&
skip() noexcept;

/**
 * The index of the long skip for a particular ledger range.
 *
 * The "long" skip list is a node that holds the hashes of (up to) 256 flag
 * ledgers.
 *
 * It can be used to efficiently skip back to any ledger using only two hops:
 * the first hop gets the "long" skip list for the ledger it wants to retrieve
 * and uses it to get the hash of the flag ledger whose short skip list will
 * contain the hash of the requested ledger.
 */
Keylet
skip(LedgerIndex ledger) noexcept;

/**
 * The (fixed) index of the object containing the ledger fees.
 */
Keylet const&
feeSettings() noexcept;

/**
 * The (fixed) index of the object containing the ledger negativeUNL.
 */
Keylet const&
negativeUNL() noexcept;

/**
 * The beginning of an order book
 */
Keylet
book(Book const& b);

/**
 * The index of a trust line for a given currency
 *
 * Note that a trustline is *shared* between two accounts (commonly referred
 * to as the issuer and the holder); if Alice sets up a trust line to Bob for
 * BTC, and Bob trusts Alice for BTC, here is only a single BTC trust line
 * between them.
 */
/**
 * @{.
 */
Keylet
trustLine(AccountID const& id0, AccountID const& id1, Currency const& currency) noexcept;

inline Keylet
trustLine(AccountID const& id, Issue const& issue) noexcept
{
    return trustLine(id, issue.account, issue.currency);
}
/**
 * @}.
 */

/**
 * An offer from an account
 */
/**
 * @{.
 */
Keylet
offer(AccountID const& id, SeqProxy const& seq) noexcept;

inline Keylet
offer(UInt256 const& key) noexcept
{
    return {ltOFFER, key};
}
/**
 * @}.
 */

/**
 * The initial directory page for a specific quality
 */
Keylet
quality(Keylet const& k, std::uint64_t const q) noexcept;

/**
 * The directory for the next lower quality
 */
Keylet
next(Keylet const& k);

/**
 * A ticket belonging to an account
 */
/**
 * @{.
 */
Keylet
ticket(AccountID const& id, SeqProxy const& ticketSeq);

inline Keylet
ticket(UInt256 const& key)
{
    return {ltTICKET, key};
}
/**
 * @}.
 */

/**
 * A SignerList
 */
Keylet
signerList(AccountID const& account) noexcept;

/**
 * A Sponsorship
 */
Keylet
sponsorship(AccountID const& sponsor, AccountID const& sponsee) noexcept;

/**
 * An account's beneficiary designation. One per account.
 */
Keylet
beneficiary(AccountID const& account) noexcept;

/**
 * A Check
 */
/**
 * @{.
 */
Keylet
check(AccountID const& id, SeqProxy const& seq) noexcept;

inline Keylet
check(UInt256 const& key) noexcept
{
    return {ltCHECK, key};
}
/**
 * @}.
 */

/**
 * A TransactionProposal
 */
/** @{ */
Keylet
txProposal(AccountID const& target, std::uint32_t ticketSequence) noexcept;

inline Keylet
txProposal(UInt256 const& key) noexcept
{
    return {ltTRANSACTION_PROPOSAL, key};
}
/** @} */

/**
 * A DepositPreauth
 */
/**
 * @{.
 */
Keylet
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept;

Keylet
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept;

inline Keylet
depositPreauth(UInt256 const& key) noexcept
{
    return {ltDEPOSIT_PREAUTH, key};
}
/**
 * @}.
 */

//------------------------------------------------------------------------------

/**
 * Any ledger entry
 */
Keylet
unchecked(UInt256 const& key) noexcept;

/**
 * The root page of an account's directory
 */
Keylet
ownerDir(AccountID const& id) noexcept;

/**
 * A page in a directory
 */
/**
 * @{.
 */
Keylet
page(UInt256 const& root, std::uint64_t const index = 0) noexcept;

inline Keylet
page(Keylet const& root, std::uint64_t const index = 0) noexcept
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::keylet::page : valid root type");
    return page(root.key, index);
}
/**
 * @}.
 */

/**
 * An escrow entry
 */
Keylet
escrow(AccountID const& src, SeqProxy const& seq) noexcept;

/**
 * A PaymentChannel
 */
Keylet
payChannel(AccountID const& src, AccountID const& dst, SeqProxy const& seq) noexcept;

/**
 * NFT page keylets
 *
 * Unlike objects whose ledger identifiers are produced by hashing data,
 * NFT page identifiers are composite identifiers, consisting of the owner's
 * 160-bit AccountID, followed by a 96-bit value that determines which NFT
 * tokens are candidates for that page.
 */
/**
 * @{.
 */
/**
 * A keylet for the owner's first possible NFT page.
 */
Keylet
nftokenPageMin(AccountID const& owner);

/**
 * A keylet for the owner's last possible NFT page.
 */
Keylet
nftokenPageMax(AccountID const& owner);

Keylet
nftokenPage(Keylet const& k, UInt256 const& token);
/** @} */

/**
 * An offer from an account to buy or sell an NFT
 */
Keylet
nftokenOffer(AccountID const& owner, SeqProxy const& seq);

inline Keylet
nftokenOffer(UInt256 const& offer)
{
    return {ltNFTOKEN_OFFER, offer};
}

/**
 * The directory of buy offers for the specified NFT
 */
Keylet
nftBuys(UInt256 const& id) noexcept;

/**
 * The directory of sell offers for the specified NFT
 */
Keylet
nftSells(UInt256 const& id) noexcept;

/**
 * AMM entry
 */
Keylet
amm(Asset const& issue1, Asset const& issue2, std::uint8_t curveType = 0) noexcept;

Keylet
amm(UInt256 const& amm) noexcept;

/**
 * A keylet for Delegate object
 */
Keylet
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept;

Keylet
did(AccountID const& account) noexcept;

Keylet
oracle(AccountID const& account, std::uint32_t const documentID) noexcept;

Keylet
credential(AccountID const& subject, AccountID const& issuer, Slice const& credType) noexcept;

inline Keylet
credential(UInt256 const& key) noexcept
{
    return {ltCREDENTIAL, key};
}

Keylet
mptokenIssuance(MPTID const& issuanceID) noexcept;

inline Keylet
mptokenIssuance(UInt256 const& issuanceKey)
{
    return {ltMPTOKEN_ISSUANCE, issuanceKey};
}

Keylet
tokenIssuance(AccountID const& issuer, Currency const& currency) noexcept;

inline Keylet
tokenIssuance(uint256 const& key)
{
    return {ltTOKEN_ISSUANCE, key};
}

Keylet
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept;

inline Keylet
mptoken(UInt256 const& mptokenKey)
{
    return {ltMPTOKEN, mptokenKey};
}

Keylet
mptoken(UInt256 const& issuanceKey, AccountID const& holder) noexcept;

Keylet
vault(AccountID const& owner, SeqProxy const& seq) noexcept;

inline Keylet
vault(UInt256 const& vaultKey)
{
    return {ltVAULT, vaultKey};
}

Keylet
loanBroker(AccountID const& owner, SeqProxy const& seq) noexcept;

/**
 * A repurchase agreement, keyed by the seller and the creating sequence.
 */
Keylet
repo(AccountID const& seller, SeqProxy const& seq) noexcept;

inline Keylet
repo(UInt256 const& repoID)
{
    return {ltREPO, repoID};
}

/**
 * A firewall, keyed by the account it protects.
 */
Keylet
firewall(AccountID const& account) noexcept;

inline Keylet
firewall(UInt256 const& firewallID)
{
    return {ltFIREWALL, firewallID};
}

/**
 * A withdraw preauthorization, keyed by owner, authorized account and tag.
 */
Keylet
withdrawPreauth(
    AccountID const& owner,
    AccountID const& preauthorized,
    std::uint32_t dtag) noexcept;

inline Keylet
withdrawPreauth(UInt256 const& key)
{
    return {ltWITHDRAW_PREAUTH, key};
}

inline Keylet
loanBroker(UInt256 const& key)
{
    return {ltLOAN_BROKER, key};
}

Keylet
loan(UInt256 const& loanBrokerID, SeqProxy const& loanSeq) noexcept;

inline Keylet
loan(UInt256 const& key)
{
    return {ltLOAN, key};
}

Keylet
couponSchedule(UInt192 const& mptIssuanceID) noexcept;

inline Keylet
couponSchedule(UInt256 const& scheduleKey)
{
    return {ltCOUPON_SCHEDULE, scheduleKey};
}

Keylet
permissionedDomain(AccountID const& account, SeqProxy const& seq) noexcept;

Keylet
permissionedDomain(UInt256 const& domainID) noexcept;

Keylet
passkeyList(AccountID const& account) noexcept;

/**
 * A ballot owned by `owner`, keyed by the creating transaction sequence.
 */
Keylet
ballot(AccountID const& owner, std::uint32_t seq) noexcept;

inline Keylet
ballot(UInt256 const& ballotID)
{
    return {ltBALLOT, ballotID};
}

/**
 * A voter's cast on the ballot identified by `ballotID`.
 */
Keylet
ballotVote(UInt256 const& ballotID, AccountID const& voter) noexcept;

inline Keylet
ballotVote(UInt256 const& key)
{
    return {ltBALLOT_VOTE, key};
}

Keylet
subscription(AccountID const& account, AccountID const& dest, std::uint32_t seq) noexcept;

inline Keylet
subscription(UInt256 const& key) noexcept
{
    return {ltSUBSCRIPTION, key};
}
/**
 * A concentrated liquidity AMM position.
 */
Keylet
ammPosition(UInt256 const& ammID, AccountID const& owner, std::uint32_t seq) noexcept;

inline Keylet
ammPosition(UInt256 const& key)
{
    return {ltAMM_POSITION, key};
}

/**
 * A concentrated liquidity AMM tick.
 * Uses structured (non-hashed) keys for ordered SHAMap traversal.
 * High 192 bits: pool scope (from ammID hash).
 * Low 64 bits: encoded tick index (offset binary plus one, big-endian), so no tick's key is the
 * base key.
 */
Keylet
ammTick(UInt256 const& ammID, std::int32_t tickIndex) noexcept;

inline Keylet
ammTick(UInt256 const& key)
{
    return {ltAMM_TICK, key};
}

/**
 * The tick index a key made by ammTick(ammID, tickIndex) encodes.
 */
std::int32_t
ammTickIndex(UInt256 const& key) noexcept;

/**
 * Base key for a CL pool's tick range (low 64 bits zeroed).
 */
Keylet
ammTickBase(UInt256 const& ammID) noexcept;

/**
 * End key for a CL pool's tick range (low 64 bits all 1s).
 */
Keylet
ammTickEnd(UInt256 const& ammID) noexcept;

/**
 * A 256-tick presence bitmap window for a CL pool.
 * Keylet structure mirrors `ammTick`: high 192 bits derive from a pool-scoped
 * hash, low 64 bits encode the word index (big-endian) so range walks via
 * SHAMap succ/pred yield the next-higher / next-lower word.
 */
Keylet
ammTickBitmapWord(UInt256 const& ammID, std::uint16_t wordIndex) noexcept;

inline Keylet
ammTickBitmapWord(UInt256 const& key)
{
    return {ltAMM_TICK_BITMAP, key};
}

/**
 * Base key for a CL pool's tick-bitmap range (low 64 bits zeroed).
 */
Keylet
ammTickBitmapBase(UInt256 const& ammID) noexcept;

/**
 * End key for a CL pool's tick-bitmap range (low 64 bits all 1s).
 */
Keylet
ammTickBitmapEnd(UInt256 const& ammID) noexcept;

/**
 * A single bin within a kCtBinned AMM pool. Bins are keyed by signed
 * bin ID, offset-encoded into the low 64 bits of the keylet so SHAMap
 * range walks yield consecutive bins in price order.
 */
Keylet
ammBin(UInt256 const& ammID, std::int32_t binID) noexcept;

/**
 * The bin ID a key made by ammBin(ammID, binID) encodes.
 */
std::int32_t
ammBinID(UInt256 const& key) noexcept;

/**
 * Lookup a bin SLE by its raw key (used by transactors that have a
 * stored issuance / bin reference).
 */
Keylet
ammBin(UInt256 const& key) noexcept;

/**
 * Base / end keys for a binned AMM's bin-SLE range. Bins for the
 * same AMM are contiguous in SHAMap order (high 192 bits are an
 * ammID-scoped hash; low 64 bits offset-encode the bin ID), so
 * `view.succ(bin_at(binID).key, ammBinEnd(ammID).key)` jumps to the
 * next populated bin in O(log n) regardless of gap size.
 */
Keylet
ammBinBase(UInt256 const& ammID) noexcept;

Keylet
ammBinEnd(UInt256 const& ammID) noexcept;

/**
 * A single LP's holding record in a single bin. Phase 5 will replace
 * this with a fungible MPT issuance per bin.
 */
Keylet
ammBinHolding(UInt256 const& ammID, AccountID const& owner, std::int32_t binID) noexcept;

Keylet
ammBinHolding(UInt256 const& key) noexcept;

}  // namespace keylet

// Everything below is deprecated and should be removed in favor of keylets:

UInt256
getBookBase(Book const& book);

UInt256
getQualityNext(UInt256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality(UInt256 const& uBase);

template <class... KeyletParams>
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct KeyletDesc
{
    std::function<Keylet(KeyletParams...)> function;
    json::StaticString expectedLEName;
    bool includeInTests{};
};

// This list should include all of the keylet functions that take a single AccountID parameter.
extern std::array<KeyletDesc<AccountID const&>, 6> const kDirectAccountKeylets;

MPTID
makeMptID(std::uint32_t const sequence, AccountID const& account);

}  // namespace xrpl
