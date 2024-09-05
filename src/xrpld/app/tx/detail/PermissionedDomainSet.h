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

#ifndef RIPPLE_TX_PERMISSIONEDDOMAINSET_H_INCLUDED
#define RIPPLE_TX_PERMISSIONEDDOMAINSET_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>
#include <optional>

namespace ripple {

class PermissionedDomainSet : public Transactor
{
public:
    static constexpr std::size_t PD_ARRAY_MAX = 10;
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit PermissionedDomainSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /** Attempt to create the Permissioned Domain. */
    TER
    doApply() override;
};

}  // namespace ripple

#endif  // RIPPLE_TX_PERMISSIONEDDOMAINSET_H_INCLUDED
