//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#ifndef XRPL_TEST_JTX_ESCROW_H_INCLUDED
#define XRPL_TEST_JTX_ESCROW_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/owners.h>
#include <test/jtx/rate.h>

#include <xrpl/protocol/Indexes.h>

namespace ripple {
namespace test {
namespace jtx {

/** Escrow operations. */
namespace escrow {

Json::Value
create(AccountID const& account, AccountID const& to, STAmount const& amount);

inline Json::Value
create(Account const& account, Account const& to, STAmount const& amount)
{
    return create(account.id(), to.id(), amount);
}

Json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq);

inline Json::Value
finish(Account const& account, Account const& from, std::uint32_t seq)
{
    return finish(account.id(), from.id(), seq);
}

Json::Value
cancel(AccountID const& account, Account const& from, std::uint32_t seq);

inline Json::Value
cancel(Account const& account, Account const& from, std::uint32_t seq)
{
    return cancel(account.id(), from, seq);
}

Rate
rate(Env& env, Account const& account, std::uint32_t const& seq);

// A PreimageSha256 fulfillments and its associated condition.
std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

std::array<std::uint8_t, 39> const cb1 = {
    {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
     0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
     0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
     0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

// Another PreimageSha256 fulfillments and its associated condition.
std::array<std::uint8_t, 7> const fb2 = {
    {0xA0, 0x05, 0x80, 0x03, 0x61, 0x61, 0x61}};

std::array<std::uint8_t, 39> const cb2 = {
    {0xA0, 0x25, 0x80, 0x20, 0x98, 0x34, 0x87, 0x6D, 0xCF, 0xB0,
     0x5C, 0xB1, 0x67, 0xA5, 0xC2, 0x49, 0x53, 0xEB, 0xA5, 0x8C,
     0x4A, 0xC8, 0x9B, 0x1A, 0xDF, 0x57, 0xF2, 0x8F, 0x2F, 0x9D,
     0x09, 0xAF, 0x10, 0x7E, 0xE8, 0xF0, 0x81, 0x01, 0x03}};

// Another PreimageSha256 fulfillment and its associated condition.
std::array<std::uint8_t, 8> const fb3 = {
    {0xA0, 0x06, 0x80, 0x04, 0x6E, 0x69, 0x6B, 0x62}};

std::array<std::uint8_t, 39> const cb3 = {
    {0xA0, 0x25, 0x80, 0x20, 0x6E, 0x4C, 0x71, 0x45, 0x30, 0xC0,
     0xA4, 0x26, 0x8B, 0x3F, 0xA6, 0x3B, 0x1B, 0x60, 0x6F, 0x2D,
     0x26, 0x4A, 0x2D, 0x85, 0x7B, 0xE8, 0xA0, 0x9C, 0x1D, 0xFD,
     0x57, 0x0D, 0x15, 0x85, 0x8B, 0xD4, 0x81, 0x01, 0x04}};

/** Set the "FinishAfter" time tag on a JTx */
auto const finish_time = JTxFieldWrapper<timePointField>(sfFinishAfter);

/** Set the "CancelAfter" time tag on a JTx */
auto const cancel_time = JTxFieldWrapper<timePointField>(sfCancelAfter);

auto const condition = JTxFieldWrapper<blobField>(sfCondition);

auto const fulfillment = JTxFieldWrapper<blobField>(sfFulfillment);

}  // namespace escrow

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif
