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

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/nftPageMask.h>

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace ripple {

/** Type-specific prefix for calculating ledger indices.

    The identifier for a given object within the ledger is calculated based
    on some object-specific parameters. To ensure that different types of
    objects have different indices, even if they happen to use the same set
    of parameters, we use "tagged hashing" by adding a type-specific prefix.

    @note These values are part of the protocol and *CANNOT* be arbitrarily
          changed. If they were, on-ledger objects may no longer be able to
          be located or addressed.

          Additions to this list are OK, but changing existing entries to
          assign them a different values should never be needed.

          Entries that are removed should be moved to the bottom of the enum
          and marked as [[deprecated]] to prevent accidental reuse.
*/
enum class LedgerNameSpace : std::uint16_t {
    ACCOUNT = 'a',
    DIR_NODE = 'd',
    TRUST_LINE = 'r',
    OFFER = 'o',
    OWNER_DIR = 'O',
    BOOK_DIR = 'B',
    SKIP_LIST = 's',
    ESCROW = 'u',
    AMENDMENTS = 'f',
    FEE_SETTINGS = 'e',
    TICKET = 'T',
    SIGNER_LIST = 'S',
    XRP_PAYMENT_CHANNEL = 'x',
    CHECK = 'C',
    DEPOSIT_PREAUTH = 'p',
    DEPOSIT_PREAUTH_CREDENTIALS = 'P',
    NEGATIVE_UNL = 'N',
    NFTOKEN_OFFER = 'q',
    NFTOKEN_BUY_OFFERS = 'h',
    NFTOKEN_SELL_OFFERS = 'i',
    AMM = 'A',
    BRIDGE = 'H',
    XCHAIN_CLAIM_ID = 'Q',
    XCHAIN_CREATE_ACCOUNT_CLAIM_ID = 'K',
    DID = 'I',
    ORACLE = 'R',
    MPTOKEN_ISSUANCE = '~',
    MPTOKEN = 't',
    CREDENTIAL = 'D',
    PERMISSIONED_DOMAIN = 'm',
    DELEGATE = 'E',
    VAULT = 'V',

    // No longer used or supported. Left here to reserve the space
    // to avoid accidental reuse.
    CONTRACT [[deprecated]] = 'c',
    GENERATOR [[deprecated]] = 'g',
    NICKNAME [[deprecated]] = 'n',
};

template <class... Args>
static uint256
indexHash(LedgerNameSpace space, Args const&... args)
{
    return sha512Half(safe_cast<std::uint16_t>(space), args...);
}

uint256
getBookBase(Book const& book)
{
    XRPL_ASSERT(
        isConsistent(book), "ripple::getBookBase : input is consistent");

    auto const index = book.domain ? indexHash(
                                         LedgerNameSpace::BOOK_DIR,
                                         book.in.currency,
                                         book.out.currency,
                                         book.in.account,
                                         book.out.account,
                                         *(book.domain))
                                   : indexHash(
                                         LedgerNameSpace::BOOK_DIR,
                                         book.in.currency,
                                         book.out.currency,
                                         book.in.account,
                                         book.out.account);

    // Return with quality 0.
    auto k = keylet::quality({index}, 0);

    return k.key;
}

uint256
getQualityNext(uint256 const& uBase)
{
    static constexpr uint256 nextq(
        "0000000000000000000000000000000000000000000000010000000000000000");
    return uBase + nextq;
}

std::uint64_t
getQuality(uint256 const& uBase)
{
    // VFALCO [base_uint] This assumes a certain storage format
    return boost::endian::big_to_native(((std::uint64_t*)uBase.end())[-1]);
}

uint256
getTicketIndex(AccountID const& account, std::uint32_t ticketSeq)
{
    return indexHash(
        LedgerNameSpace::TICKET, account, std::uint32_t(ticketSeq));
}

uint256
getTicketIndex(AccountID const& account, SeqProxy ticketSeq)
{
    XRPL_ASSERT(ticketSeq.isTicket(), "ripple::getTicketIndex : valid input");
    return getTicketIndex(account, ticketSeq.value());
}

MPTID
makeMptID(std::uint32_t sequence, AccountID const& account)
{
    MPTID u;
    sequence = boost::endian::native_to_big(sequence);
    memcpy(u.data(), &sequence, sizeof(sequence));
    memcpy(u.data() + sizeof(sequence), account.data(), sizeof(account));
    return u;
}

//------------------------------------------------------------------------------

namespace keylet {

TypedKeylet<ltACCOUNT_ROOT>
account(AccountID const& id) noexcept
{
    return TypedKeylet<ltACCOUNT_ROOT>{indexHash(LedgerNameSpace::ACCOUNT, id)};
}

Keylet
child(uint256 const& key) noexcept
{
    return {ltCHILD, key};
}

TypedKeylet<ltLEDGER_HASHES> const&
skip() noexcept
{
    static TypedKeylet<ltLEDGER_HASHES> const ret{
        indexHash(LedgerNameSpace::SKIP_LIST)};
    return ret;
}

TypedKeylet<ltLEDGER_HASHES>
skip(LedgerIndex ledger) noexcept
{
    return {indexHash(
        LedgerNameSpace::SKIP_LIST,
        std::uint32_t(static_cast<std::uint32_t>(ledger) >> 16))};
}

TypedKeylet<ltAMENDMENTS> const&
amendments() noexcept
{
    static TypedKeylet<ltAMENDMENTS> const ret{
        indexHash(LedgerNameSpace::AMENDMENTS)};
    return ret;
}

TypedKeylet<ltFEE_SETTINGS> const&
fees() noexcept
{
    static TypedKeylet<ltFEE_SETTINGS> const ret{
        indexHash(LedgerNameSpace::FEE_SETTINGS)};
    return ret;
}

TypedKeylet<ltNEGATIVE_UNL> const&
negativeUNL() noexcept
{
    static TypedKeylet<ltNEGATIVE_UNL> const ret{
        indexHash(LedgerNameSpace::NEGATIVE_UNL)};
    return ret;
}

TypedKeylet<ltDIR_NODE>
book_t::operator()(Book const& b) const
{
    return {getBookBase(b)};
}

TypedKeylet<ltRIPPLE_STATE>
line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept
{
    // There is code in SetTrust that calls us with id0 == id1, to allow users
    // to locate and delete such "weird" trustlines. If we remove that code, we
    // could enable this assert:
    // XRPL_ASSERT(id0 != id1, "ripple::keylet::line : accounts must be
    // different");

    // A trust line is shared between two accounts; while we typically think
    // of this as an "issuer" and a "holder" the relationship is actually fully
    // bidirectional.
    //
    // So that we can generate a unique ID for a trust line, regardess of which
    // side of the line we're looking at, we define a "canonical" order for the
    // two accounts (smallest then largest)  and hash them in that order:
    auto const accounts = std::minmax(id0, id1);

    return {indexHash(
        LedgerNameSpace::TRUST_LINE,
        accounts.first,
        accounts.second,
        currency)};
}

TypedKeylet<ltOFFER>
offer(AccountID const& id, std::uint32_t seq) noexcept
{
    return {indexHash(LedgerNameSpace::OFFER, id, seq)};
}

TypedKeylet<ltDIR_NODE>
quality(TypedKeylet<ltDIR_NODE> const& k, std::uint64_t q) noexcept
{
    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first and the least significant bytes
    // represent adjacent entries. We place the quality, in big endian format,
    // in the 8 right most bytes; this way, incrementing goes to the next entry
    // for indexes.
    uint256 x = k.key;

    // FIXME This is ugly and we can and should do better...
    ((std::uint64_t*)x.end())[-1] = boost::endian::native_to_big(q);

    return {x};
}

TypedKeylet<ltDIR_NODE>
next_t::operator()(TypedKeylet<ltDIR_NODE> const& k) const
{
    return {getQualityNext(k.key)};
}

TypedKeylet<ltTICKET>
ticket_t::operator()(AccountID const& id, std::uint32_t ticketSeq) const
{
    return {getTicketIndex(id, ticketSeq)};
}

TypedKeylet<ltTICKET>
ticket_t::operator()(AccountID const& id, SeqProxy ticketSeq) const
{
    return {getTicketIndex(id, ticketSeq)};
}

// This function is presently static, since it's never accessed from anywhere
// else. If we ever support multiple pages of signer lists, this would be the
// keylet used to locate them.
static TypedKeylet<ltSIGNER_LIST>
signers(AccountID const& account, std::uint32_t page) noexcept
{
    return {indexHash(LedgerNameSpace::SIGNER_LIST, account, page)};
}

TypedKeylet<ltSIGNER_LIST>
signers(AccountID const& account) noexcept
{
    return signers(account, 0);
}

TypedKeylet<ltCHECK>
check(AccountID const& id, std::uint32_t seq) noexcept
{
    return TypedKeylet<ltCHECK>{indexHash(LedgerNameSpace::CHECK, id, seq)};
}

TypedKeylet<ltDEPOSIT_PREAUTH>
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept
{
    return TypedKeylet<ltDEPOSIT_PREAUTH>{
        indexHash(LedgerNameSpace::DEPOSIT_PREAUTH, owner, preauthorized)};
}

// Credentials should be sorted here, use credentials::makeSorted
TypedKeylet<ltDEPOSIT_PREAUTH>
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept
{
    std::vector<uint256> hashes;
    hashes.reserve(authCreds.size());
    for (auto const& o : authCreds)
        hashes.emplace_back(sha512Half(o.first, o.second));

    return TypedKeylet<ltDEPOSIT_PREAUTH>{
        indexHash(LedgerNameSpace::DEPOSIT_PREAUTH_CREDENTIALS, owner, hashes)};
}

//------------------------------------------------------------------------------

Keylet
unchecked(uint256 const& key) noexcept
{
    return Keylet{ltANY, key};
}

TypedKeylet<ltDIR_NODE>
ownerDir(AccountID const& id) noexcept
{
    return TypedKeylet<ltDIR_NODE>{indexHash(LedgerNameSpace::OWNER_DIR, id)};
}

TypedKeylet<ltDIR_NODE>
page(uint256 const& key, std::uint64_t index) noexcept
{
    if (index == 0)
        return TypedKeylet<ltDIR_NODE>{key};

    return TypedKeylet<ltDIR_NODE>{
        indexHash(LedgerNameSpace::DIR_NODE, key, index)};
}

TypedKeylet<ltESCROW>
escrow(AccountID const& src, std::uint32_t seq) noexcept
{
    return TypedKeylet<ltESCROW>{indexHash(LedgerNameSpace::ESCROW, src, seq)};
}

TypedKeylet<ltPAYCHAN>
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept
{
    return TypedKeylet<ltPAYCHAN>{
        indexHash(LedgerNameSpace::XRP_PAYMENT_CHANNEL, src, dst, seq)};
}

TypedKeylet<ltNFTOKEN_PAGE>
nftpage_min(AccountID const& owner)
{
    std::array<std::uint8_t, 32> buf{};
    std::memcpy(buf.data(), owner.data(), owner.size());
    return TypedKeylet<ltNFTOKEN_PAGE>{uint256{buf}};
}

TypedKeylet<ltNFTOKEN_PAGE>
nftpage_max(AccountID const& owner)
{
    uint256 id = nft::pageMask;
    std::memcpy(id.data(), owner.data(), owner.size());
    return TypedKeylet<ltNFTOKEN_PAGE>{id};
}

TypedKeylet<ltNFTOKEN_PAGE>
nftpage(TypedKeylet<ltNFTOKEN_PAGE> const& k, uint256 const& token)
{
    return {(k.key & ~nft::pageMask) + (token & nft::pageMask)};
}

TypedKeylet<ltNFTOKEN_OFFER>
nftoffer(AccountID const& owner, std::uint32_t seq)
{
    return TypedKeylet<ltNFTOKEN_OFFER>{
        indexHash(LedgerNameSpace::NFTOKEN_OFFER, owner, seq)};
}

TypedKeylet<ltDIR_NODE>
nft_buys(uint256 const& id) noexcept
{
    return TypedKeylet<ltDIR_NODE>{
        indexHash(LedgerNameSpace::NFTOKEN_BUY_OFFERS, id)};
}

TypedKeylet<ltDIR_NODE>
nft_sells(uint256 const& id) noexcept
{
    return TypedKeylet<ltDIR_NODE>{
        indexHash(LedgerNameSpace::NFTOKEN_SELL_OFFERS, id)};
}

TypedKeylet<ltAMM>
amm(Asset const& issue1, Asset const& issue2) noexcept
{
    auto const& [minI, maxI] =
        std::minmax(issue1.get<Issue>(), issue2.get<Issue>());
    return amm(indexHash(
        LedgerNameSpace::AMM,
        minI.account,
        minI.currency,
        maxI.account,
        maxI.currency));
}

TypedKeylet<ltAMM>
amm(uint256 const& id) noexcept
{
    return TypedKeylet<ltAMM>{id};
}

TypedKeylet<ltDELEGATE>
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept
{
    return TypedKeylet<ltDELEGATE>{
        indexHash(LedgerNameSpace::DELEGATE, account, authorizedAccount)};
}

TypedKeylet<ltBRIDGE>
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType)
{
    // A door account can support multiple bridges. On the locking chain
    // there can only be one bridge per lockingChainCurrency. On the issuing
    // chain there can only be one bridge per issuingChainCurrency.
    auto const& issue = bridge.issue(chainType);
    return TypedKeylet<ltBRIDGE>{indexHash(
        LedgerNameSpace::BRIDGE, bridge.door(chainType), issue.currency)};
}

TypedKeylet<ltXCHAIN_OWNED_CLAIM_ID>
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return TypedKeylet<ltXCHAIN_OWNED_CLAIM_ID>{indexHash(
        LedgerNameSpace::XCHAIN_CLAIM_ID,
        bridge.lockingChainDoor(),
        bridge.lockingChainIssue(),
        bridge.issuingChainDoor(),
        bridge.issuingChainIssue(),
        seq)};
}

TypedKeylet<ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID>
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return TypedKeylet<ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID>{indexHash(
        LedgerNameSpace::XCHAIN_CREATE_ACCOUNT_CLAIM_ID,
        bridge.lockingChainDoor(),
        bridge.lockingChainIssue(),
        bridge.issuingChainDoor(),
        bridge.issuingChainIssue(),
        seq)};
}

TypedKeylet<ltDID>
did(AccountID const& account) noexcept
{
    return TypedKeylet<ltDID>{indexHash(LedgerNameSpace::DID, account)};
}

TypedKeylet<ltORACLE>
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept
{
    return TypedKeylet<ltORACLE>{
        indexHash(LedgerNameSpace::ORACLE, account, documentID)};
}

TypedKeylet<ltMPTOKEN_ISSUANCE>
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept
{
    return mptIssuance(makeMptID(seq, issuer));
}

TypedKeylet<ltMPTOKEN_ISSUANCE>
mptIssuance(MPTID const& issuanceID) noexcept
{
    return TypedKeylet<ltMPTOKEN_ISSUANCE>{
        indexHash(LedgerNameSpace::MPTOKEN_ISSUANCE, issuanceID)};
}

TypedKeylet<ltMPTOKEN>
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept
{
    return mptoken(mptIssuance(issuanceID).key, holder);
}

TypedKeylet<ltMPTOKEN>
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept
{
    return TypedKeylet<ltMPTOKEN>{
        indexHash(LedgerNameSpace::MPTOKEN, issuanceKey, holder)};
}

TypedKeylet<ltCREDENTIAL>
credential(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credType) noexcept
{
    return TypedKeylet<ltCREDENTIAL>{
        indexHash(LedgerNameSpace::CREDENTIAL, subject, issuer, credType)};
}

TypedKeylet<ltVAULT>
vault(AccountID const& owner, std::uint32_t seq) noexcept
{
    return vault(indexHash(LedgerNameSpace::VAULT, owner, seq));
}

TypedKeylet<ltPERMISSIONED_DOMAIN>
permissionedDomain(AccountID const& account, std::uint32_t seq) noexcept
{
    return TypedKeylet<ltPERMISSIONED_DOMAIN>{
        indexHash(LedgerNameSpace::PERMISSIONED_DOMAIN, account, seq)};
}

TypedKeylet<ltPERMISSIONED_DOMAIN>
permissionedDomain(uint256 const& domainID) noexcept
{
    return TypedKeylet<ltPERMISSIONED_DOMAIN>{domainID};
}

}  // namespace keylet

}  // namespace ripple
