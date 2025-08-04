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

#ifndef RIPPLE_PROTOCOL_INDEXES_H_INCLUDED
#define RIPPLE_PROTOCOL_INDEXES_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace ripple {

class SeqProxy;
/** Keylet computation funclets.

    Entries in the ledger are located using 256-bit locators. The locators are
    calculated using a wide range of parameters specific to the entry whose
    locator we are calculating (e.g. an account's locator is derived from the
    account's address, whereas the locator for an offer is derived from the
    account and the offer sequence.)

    To enhance type safety during lookup and make the code more robust, we use
    keylets, which contain not only the locator of the object but also the type
    of the object being referenced.

    These functions each return a type-specific keylet.
*/
namespace keylet {

/** AccountID root */
TypedKeylet<ltACCOUNT_ROOT>
account(AccountID const& id) noexcept;

/** The index of the amendment table */
TypedKeylet<ltAMENDMENTS> const&
amendments() noexcept;

/** Any item that can be in an owner dir. */
Keylet
child(uint256 const& key) noexcept;

/** The index of the "short" skip list

    The "short" skip list is a node (at a fixed index) that holds the hashes
    of ledgers since the last flag ledger. It will contain, at most, 256 hashes.
*/
TypedKeylet<ltLEDGER_HASHES> const&
skip() noexcept;

/** The index of the long skip for a particular ledger range.

    The "long" skip list is a node that holds the hashes of (up to) 256 flag
    ledgers.

    It can be used to efficiently skip back to any ledger using only two hops:
    the first hop gets the "long" skip list for the ledger it wants to retrieve
    and uses it to get the hash of the flag ledger whose short skip list will
    contain the hash of the requested ledger.
*/
TypedKeylet<ltLEDGER_HASHES>
skip(LedgerIndex ledger) noexcept;

/** The (fixed) index of the object containing the ledger fees. */
TypedKeylet<ltFEE_SETTINGS> const&
fees() noexcept;

/** The (fixed) index of the object containing the ledger negativeUNL. */
TypedKeylet<ltNEGATIVE_UNL> const&
negativeUNL() noexcept;

/** The beginning of an order book */
struct book_t
{
    explicit book_t() = default;

    TypedKeylet<ltDIR_NODE>
    operator()(Book const& b) const;
};
static book_t const book{};

/** The index of a trust line for a given currency

    Note that a trustline is *shared* between two accounts (commonly referred
    to as the issuer and the holder); if Alice sets up a trust line to Bob for
    BTC, and Bob trusts Alice for BTC, here is only a single BTC trust line
    between them.
*/
/** @{ */
TypedKeylet<ltRIPPLE_STATE>
line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept;

inline TypedKeylet<ltRIPPLE_STATE>
line(AccountID const& id, Issue const& issue) noexcept
{
    return line(id, issue.account, issue.currency);
}
/** @} */

/** An offer from an account */
/** @{ */
TypedKeylet<ltOFFER>
offer(AccountID const& id, std::uint32_t seq) noexcept;

inline TypedKeylet<ltOFFER>
offer(uint256 const& key) noexcept
{
    return {key};
}
/** @} */

/** The initial directory page for a specific quality */
TypedKeylet<ltDIR_NODE>
quality(TypedKeylet<ltDIR_NODE> const& k, std::uint64_t q) noexcept;

/** The directory for the next lower quality */
struct next_t
{
    explicit next_t() = default;

    TypedKeylet<ltDIR_NODE>
    operator()(TypedKeylet<ltDIR_NODE> const& k) const;
};
static next_t const next{};

/** A ticket belonging to an account */
struct ticket_t
{
    explicit ticket_t() = default;

    TypedKeylet<ltTICKET>
    operator()(AccountID const& id, std::uint32_t ticketSeq) const;

    TypedKeylet<ltTICKET>
    operator()(AccountID const& id, SeqProxy ticketSeq) const;

    TypedKeylet<ltTICKET>
    operator()(uint256 const& key) const
    {
        return {key};
    }
};
static ticket_t const ticket{};

/** A SignerList */
TypedKeylet<ltSIGNER_LIST>
signers(AccountID const& account) noexcept;

/** A Check */
/** @{ */
TypedKeylet<ltCHECK>
check(AccountID const& id, std::uint32_t seq) noexcept;

inline TypedKeylet<ltCHECK>
check(uint256 const& key) noexcept
{
    return {key};
}
/** @} */

/** A DepositPreauth */
/** @{ */
TypedKeylet<ltDEPOSIT_PREAUTH>
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept;

TypedKeylet<ltDEPOSIT_PREAUTH>
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept;

inline TypedKeylet<ltDEPOSIT_PREAUTH>
depositPreauth(uint256 const& key) noexcept
{
    return {key};
}
/** @} */

//------------------------------------------------------------------------------

/** Any ledger entry */
Keylet
unchecked(uint256 const& key) noexcept;

/** The root page of an account's directory */
TypedKeylet<ltDIR_NODE>
ownerDir(AccountID const& id) noexcept;

/** A page in a directory */
/** @{ */
TypedKeylet<ltDIR_NODE>
page(uint256 const& root, std::uint64_t index = 0) noexcept;

inline TypedKeylet<ltDIR_NODE>
page(Keylet const& root, std::uint64_t index = 0) noexcept
{
    XRPL_ASSERT(
        root.type == ltDIR_NODE, "ripple::keylet::page : valid root type");
    return page(root.key, index);
}
/** @} */

/** An escrow entry */
TypedKeylet<ltESCROW>
escrow(AccountID const& src, std::uint32_t seq) noexcept;

/** A PaymentChannel */
TypedKeylet<ltPAYCHAN>
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept;

/** NFT page keylets

    Unlike objects whose ledger identifiers are produced by hashing data,
    NFT page identifiers are composite identifiers, consisting of the owner's
    160-bit AccountID, followed by a 96-bit value that determines which NFT
    tokens are candidates for that page.
 */
/** @{ */
/** A keylet for the owner's first possible NFT page. */
TypedKeylet<ltNFTOKEN_PAGE>
nftpage_min(AccountID const& owner);

/** A keylet for the owner's last possible NFT page. */
TypedKeylet<ltNFTOKEN_PAGE>
nftpage_max(AccountID const& owner);

TypedKeylet<ltNFTOKEN_PAGE>
nftpage(TypedKeylet<ltNFTOKEN_PAGE> const& k, uint256 const& token);
/** @} */

/** An offer from an account to buy or sell an NFT */
TypedKeylet<ltNFTOKEN_OFFER>
nftoffer(AccountID const& owner, std::uint32_t seq);

inline TypedKeylet<ltNFTOKEN_OFFER>
nftoffer(uint256 const& offer)
{
    return {offer};
}

/** The directory of buy offers for the specified NFT */
TypedKeylet<ltDIR_NODE>
nft_buys(uint256 const& id) noexcept;

/** The directory of sell offers for the specified NFT */
TypedKeylet<ltDIR_NODE>
nft_sells(uint256 const& id) noexcept;

/** AMM entry */
TypedKeylet<ltAMM>
amm(Asset const& issue1, Asset const& issue2) noexcept;

TypedKeylet<ltAMM>
amm(uint256 const& amm) noexcept;

/** A keylet for Delegate object */
TypedKeylet<ltDELEGATE>
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept;

TypedKeylet<ltBRIDGE>
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType);

TypedKeylet<ltXCHAIN_OWNED_CLAIM_ID>
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq);

TypedKeylet<ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID>
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq);

TypedKeylet<ltDID>
did(AccountID const& account) noexcept;

TypedKeylet<ltORACLE>
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept;

TypedKeylet<ltCREDENTIAL>
credential(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credType) noexcept;

inline TypedKeylet<ltCREDENTIAL>
credential(uint256 const& key) noexcept
{
    return {key};
}

TypedKeylet<ltMPTOKEN_ISSUANCE>
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept;

TypedKeylet<ltMPTOKEN_ISSUANCE>
mptIssuance(MPTID const& issuanceID) noexcept;

inline TypedKeylet<ltMPTOKEN_ISSUANCE>
mptIssuance(uint256 const& issuanceKey)
{
    return {issuanceKey};
}

TypedKeylet<ltMPTOKEN>
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept;

inline TypedKeylet<ltMPTOKEN>
mptoken(uint256 const& mptokenKey)
{
    return {mptokenKey};
}

TypedKeylet<ltMPTOKEN>
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept;

TypedKeylet<ltVAULT>
vault(AccountID const& owner, std::uint32_t seq) noexcept;

inline TypedKeylet<ltVAULT>
vault(uint256 const& vaultKey)
{
    return {vaultKey};
}

TypedKeylet<ltPERMISSIONED_DOMAIN>
permissionedDomain(AccountID const& account, std::uint32_t seq) noexcept;

TypedKeylet<ltPERMISSIONED_DOMAIN>
permissionedDomain(uint256 const& domainID) noexcept;
}  // namespace keylet

// Everything below is deprecated and should be removed in favor of keylets:

uint256
getBookBase(Book const& book);

uint256
getQualityNext(uint256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality(uint256 const& uBase);

uint256
getTicketIndex(AccountID const& account, std::uint32_t uSequence);

uint256
getTicketIndex(AccountID const& account, SeqProxy ticketSeq);

template <class... keyletParams>
struct keyletDesc
{
    std::function<Keylet(keyletParams...)> function;
    Json::StaticString expectedLEName;
    bool includeInTests;
};

// This list should include all of the keylet functions that take a single
// AccountID parameter.
std::array<keyletDesc<AccountID const&>, 6> const directAccountKeylets{
    {{&keylet::account, jss::AccountRoot, false},
     {&keylet::ownerDir, jss::DirectoryNode, true},
     {&keylet::signers, jss::SignerList, true},
     // It's normally impossible to create an item at nftpage_min, but
     // test it anyway, since the invariant checks for it.
     {&keylet::nftpage_min, jss::NFTokenPage, true},
     {&keylet::nftpage_max, jss::NFTokenPage, true},
     {&keylet::did, jss::DID, true}}};

MPTID
makeMptID(std::uint32_t sequence, AccountID const& account);

}  // namespace ripple

#endif
