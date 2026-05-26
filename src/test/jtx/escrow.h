#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/owners.h>
#include <test/jtx/rate.h>

#include <xrpl/protocol/Indexes.h>

/** Escrow operations. */
namespace xrpl::test::jtx::escrow {

json::Value
create(AccountID const& account, AccountID const& to, STAmount const& amount);

inline json::Value
create(Account const& account, Account const& to, STAmount const& amount)
{
    return create(account.id(), to.id(), amount);
}

json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq);

inline json::Value
finish(Account const& account, Account const& from, std::uint32_t seq)
{
    return finish(account.id(), from.id(), seq);
}

json::Value
cancel(AccountID const& account, Account const& from, std::uint32_t seq);

inline json::Value
cancel(Account const& account, Account const& from, std::uint32_t seq)
{
    return cancel(account.id(), from, seq);
}

Rate
rate(Env& env, Account const& account, std::uint32_t const& seq);

// A PreimageSha256 fulfillments and its associated kCondition.
std::array<std::uint8_t, 4> const kFb1 = {{0xA0, 0x02, 0x80, 0x00}};

std::array<std::uint8_t, 39> const kCb1 = {
    {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A,
     0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24, 0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B,
     0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

// Another PreimageSha256 fulfillments and its associated kCondition.
std::array<std::uint8_t, 7> const kFb2 = {{0xA0, 0x05, 0x80, 0x03, 0x61, 0x61, 0x61}};

std::array<std::uint8_t, 39> const kCb2 = {
    {0xA0, 0x25, 0x80, 0x20, 0x98, 0x34, 0x87, 0x6D, 0xCF, 0xB0, 0x5C, 0xB1, 0x67,
     0xA5, 0xC2, 0x49, 0x53, 0xEB, 0xA5, 0x8C, 0x4A, 0xC8, 0x9B, 0x1A, 0xDF, 0x57,
     0xF2, 0x8F, 0x2F, 0x9D, 0x09, 0xAF, 0x10, 0x7E, 0xE8, 0xF0, 0x81, 0x01, 0x03}};

// Another PreimageSha256 kFulfillment and its associated kCondition.
std::array<std::uint8_t, 8> const kFb3 = {{0xA0, 0x06, 0x80, 0x04, 0x6E, 0x69, 0x6B, 0x62}};

std::array<std::uint8_t, 39> const kCb3 = {
    {0xA0, 0x25, 0x80, 0x20, 0x6E, 0x4C, 0x71, 0x45, 0x30, 0xC0, 0xA4, 0x26, 0x8B,
     0x3F, 0xA6, 0x3B, 0x1B, 0x60, 0x6F, 0x2D, 0x26, 0x4A, 0x2D, 0x85, 0x7B, 0xE8,
     0xA0, 0x9C, 0x1D, 0xFD, 0x57, 0x0D, 0x15, 0x85, 0x8B, 0xD4, 0x81, 0x01, 0x04}};

/** Set the "FinishAfter" time tag on a JTx */
auto const kFinishTime = JTxFieldWrapper<TimePointField>(sfFinishAfter);

/** Set the "CancelAfter" time tag on a JTx */
auto const kCancelTime = JTxFieldWrapper<TimePointField>(sfCancelAfter);

auto const kCondition = JTxFieldWrapper<BlobField>(sfCondition);

auto const kFulfillment = JTxFieldWrapper<BlobField>(sfFulfillment);

}  // namespace xrpl::test::jtx::escrow
