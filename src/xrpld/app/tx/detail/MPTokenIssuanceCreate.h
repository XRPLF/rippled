//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef XRPL_TX_MPTOKENISSUANCECREATE_H_INCLUDED
#define XRPL_TX_MPTOKENISSUANCECREATE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

struct MPTCreateArgs
{
    std::optional<XRPAmount> priorBalance;
    AccountID const& account;
    std::uint32_t sequence;
    std::uint32_t flags = 0;
    std::optional<std::uint64_t> maxAmount{};
    std::optional<std::uint8_t> assetScale{};
    std::optional<std::uint16_t> transferFee{};
    std::optional<Slice> const& metadata{};
    std::optional<uint256> domainId{};
    std::optional<std::uint32_t> mutableFlags{};
};

class MPTokenIssuanceCreate : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit MPTokenIssuanceCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;

    static Expected<MPTID, TER>
    create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args);
};

}  // namespace ripple

#endif
