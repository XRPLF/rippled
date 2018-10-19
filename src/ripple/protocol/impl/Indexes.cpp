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

#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/SeqProxy.h>
#include <ripple/protocol/digest.h>
#include <boost/endian/conversion.hpp>
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
    NEGATIVE_UNL = 'N',

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
    static uint256 const uNext(from_hex_text<uint256>("10000000000000000"));
    return uBase + uNext;
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

//------------------------------------------------------------------------------

namespace keylet {

Keylet
account(AccountID const& id) noexcept
{
    return {ltACCOUNT_ROOT, indexHash(LedgerNameSpace::ACCOUNT, id)};
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

}  // namespace keylet

}  // namespace ripple
