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

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/nftPageMask.h>

#include <algorithm>
#include <cassert>

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
    assert(isConsistent(book));

    auto const index = indexHash(
        LedgerNameSpace::BOOK_DIR,
        book.in.currency,
        book.out.currency,
        book.in.account,
        book.out.account);

    // Return with quality 0.
    auto k = keylet::quality({ltDIR_NODE, index}, 0);

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
    assert(ticketSeq.isTicket());
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

Keylet
account(AccountID const& id) noexcept
{
    return Keylet{ltACCOUNT_ROOT, indexHash(LedgerNameSpace::ACCOUNT, id)};
}

Keylet
child(uint256 const& key) noexcept
{
    return {ltCHILD, key};
}

Keylet const&
skip() noexcept
{
    static Keylet const ret{
        ltLEDGER_HASHES, indexHash(LedgerNameSpace::SKIP_LIST)};
    return ret;
}

Keylet
skip(LedgerIndex ledger) noexcept
{
    return {
        ltLEDGER_HASHES,
        indexHash(
            LedgerNameSpace::SKIP_LIST,
            std::uint32_t(static_cast<std::uint32_t>(ledger) >> 16))};
}

Keylet const&
amendments() noexcept
{
    static Keylet const ret{
        ltAMENDMENTS, indexHash(LedgerNameSpace::AMENDMENTS)};
    return ret;
}

Keylet const&
fees() noexcept
{
    static Keylet const ret{
        ltFEE_SETTINGS, indexHash(LedgerNameSpace::FEE_SETTINGS)};
    return ret;
}

Keylet const&
negativeUNL() noexcept
{
    static Keylet const ret{
        ltNEGATIVE_UNL, indexHash(LedgerNameSpace::NEGATIVE_UNL)};
    return ret;
}

Keylet
book_t::operator()(Book const& b) const
{
    return {ltDIR_NODE, getBookBase(b)};
}

Keylet
line(
    AccountID const& id0,
    AccountID const& id1,
    Currency const& currency) noexcept
{
    // There is code in SetTrust that calls us with id0 == id1, to allow users
    // to locate and delete such "weird" trustlines. If we remove that code, we
    // could enable this assert:
    // assert(id0 != id1);

    // A trust line is shared between two accounts; while we typically think
    // of this as an "issuer" and a "holder" the relationship is actually fully
    // bidirectional.
    //
    // So that we can generate a unique ID for a trust line, regardess of which
    // side of the line we're looking at, we define a "canonical" order for the
    // two accounts (smallest then largest)  and hash them in that order:
    auto const accounts = std::minmax(id0, id1);

    return {
        ltRIPPLE_STATE,
        indexHash(
            LedgerNameSpace::TRUST_LINE,
            accounts.first,
            accounts.second,
            currency)};
}

Keylet
offer(AccountID const& id, std::uint32_t seq) noexcept
{
    return {ltOFFER, indexHash(LedgerNameSpace::OFFER, id, seq)};
}

Keylet
quality(Keylet const& k, std::uint64_t q) noexcept
{
    assert(k.type == ltDIR_NODE);

    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first and the least significant bytes
    // represent adjacent entries. We place the quality, in big endian format,
    // in the 8 right most bytes; this way, incrementing goes to the next entry
    // for indexes.
    uint256 x = k.key;

    // FIXME This is ugly and we can and should do better...
    ((std::uint64_t*)x.end())[-1] = boost::endian::native_to_big(q);

    return {ltDIR_NODE, x};
}

Keylet
next_t::operator()(Keylet const& k) const
{
    assert(k.type == ltDIR_NODE);
    return {ltDIR_NODE, getQualityNext(k.key)};
}

Keylet
ticket_t::operator()(AccountID const& id, std::uint32_t ticketSeq) const
{
    return {ltTICKET, getTicketIndex(id, ticketSeq)};
}

Keylet
ticket_t::operator()(AccountID const& id, SeqProxy ticketSeq) const
{
    return {ltTICKET, getTicketIndex(id, ticketSeq)};
}

// This function is presently static, since it's never accessed from anywhere
// else. If we ever support multiple pages of signer lists, this would be the
// keylet used to locate them.
static Keylet
signers(AccountID const& account, std::uint32_t page) noexcept
{
    return {
        ltSIGNER_LIST, indexHash(LedgerNameSpace::SIGNER_LIST, account, page)};
}

Keylet
signers(AccountID const& account) noexcept
{
    return signers(account, 0);
}

Keylet
check(AccountID const& id, std::uint32_t seq) noexcept
{
    return {ltCHECK, indexHash(LedgerNameSpace::CHECK, id, seq)};
}

Keylet
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept
{
    return {
        ltDEPOSIT_PREAUTH,
        indexHash(LedgerNameSpace::DEPOSIT_PREAUTH, owner, preauthorized)};
}

// Credentials should be sorted here, use credentials::makeSorted
Keylet
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept
{
    std::vector<uint256> hashes;
    hashes.reserve(authCreds.size());
    for (auto const& o : authCreds)
        hashes.emplace_back(sha512Half(o.first, o.second));

    return {
        ltDEPOSIT_PREAUTH,
        indexHash(LedgerNameSpace::DEPOSIT_PREAUTH_CREDENTIALS, owner, hashes)};
}

//------------------------------------------------------------------------------

Keylet
unchecked(uint256 const& key) noexcept
{
    return {ltANY, key};
}

Keylet
ownerDir(AccountID const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::OWNER_DIR, id)};
}

Keylet
page(uint256 const& key, std::uint64_t index) noexcept
{
    if (index == 0)
        return {ltDIR_NODE, key};

    return {ltDIR_NODE, indexHash(LedgerNameSpace::DIR_NODE, key, index)};
}

Keylet
escrow(AccountID const& src, std::uint32_t seq) noexcept
{
    return {ltESCROW, indexHash(LedgerNameSpace::ESCROW, src, seq)};
}

Keylet
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept
{
    return {
        ltPAYCHAN,
        indexHash(LedgerNameSpace::XRP_PAYMENT_CHANNEL, src, dst, seq)};
}

Keylet
nftpage_min(AccountID const& owner)
{
    std::array<std::uint8_t, 32> buf{};
    std::memcpy(buf.data(), owner.data(), owner.size());
    return {ltNFTOKEN_PAGE, uint256{buf}};
}

Keylet
nftpage_max(AccountID const& owner)
{
    uint256 id = nft::pageMask;
    std::memcpy(id.data(), owner.data(), owner.size());
    return {ltNFTOKEN_PAGE, id};
}

Keylet
nftpage(Keylet const& k, uint256 const& token)
{
    assert(k.type == ltNFTOKEN_PAGE);
    return {ltNFTOKEN_PAGE, (k.key & ~nft::pageMask) + (token & nft::pageMask)};
}

Keylet
nftoffer(AccountID const& owner, std::uint32_t seq)
{
    return {
        ltNFTOKEN_OFFER, indexHash(LedgerNameSpace::NFTOKEN_OFFER, owner, seq)};
}

Keylet
nft_buys(uint256 const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::NFTOKEN_BUY_OFFERS, id)};
}

Keylet
nft_sells(uint256 const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::NFTOKEN_SELL_OFFERS, id)};
}

Keylet
amm(Issue const& issue1, Issue const& issue2) noexcept
{
    auto const& [minI, maxI] = std::minmax(issue1, issue2);
    return amm(indexHash(
        LedgerNameSpace::AMM,
        minI.account,
        minI.currency,
        maxI.account,
        maxI.currency));
}

Keylet
amm(uint256 const& id) noexcept
{
    return {ltAMM, id};
}

Keylet
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType)
{
    // A door account can support multiple bridges. On the locking chain
    // there can only be one bridge per lockingChainCurrency. On the issuing
    // chain there can only be one bridge per issuingChainCurrency.
    auto const& issue = bridge.issue(chainType);
    return {
        ltBRIDGE,
        indexHash(
            LedgerNameSpace::BRIDGE, bridge.door(chainType), issue.currency)};
}

Keylet
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return {
        ltXCHAIN_OWNED_CLAIM_ID,
        indexHash(
            LedgerNameSpace::XCHAIN_CLAIM_ID,
            bridge.lockingChainDoor(),
            bridge.lockingChainIssue(),
            bridge.issuingChainDoor(),
            bridge.issuingChainIssue(),
            seq)};
}

Keylet
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return {
        ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID,
        indexHash(
            LedgerNameSpace::XCHAIN_CREATE_ACCOUNT_CLAIM_ID,
            bridge.lockingChainDoor(),
            bridge.lockingChainIssue(),
            bridge.issuingChainDoor(),
            bridge.issuingChainIssue(),
            seq)};
}

Keylet
did(AccountID const& account) noexcept
{
    return {ltDID, indexHash(LedgerNameSpace::DID, account)};
}

Keylet
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept
{
    return {ltORACLE, indexHash(LedgerNameSpace::ORACLE, account, documentID)};
}

Keylet
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept
{
    return mptIssuance(makeMptID(seq, issuer));
}

Keylet
mptIssuance(MPTID const& issuanceID) noexcept
{
    return {
        ltMPTOKEN_ISSUANCE,
        indexHash(LedgerNameSpace::MPTOKEN_ISSUANCE, issuanceID)};
}

Keylet
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept
{
    return mptoken(mptIssuance(issuanceID).key, holder);
}

Keylet
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept
{
    return {
        ltMPTOKEN, indexHash(LedgerNameSpace::MPTOKEN, issuanceKey, holder)};
}

Keylet
credential(
    AccountID const& subject,
    AccountID const& issuer,
    Slice const& credType) noexcept
{
    return {
        ltCREDENTIAL,
        indexHash(LedgerNameSpace::CREDENTIAL, subject, issuer, credType)};
}

}  // namespace keylet

}  // namespace ripple
