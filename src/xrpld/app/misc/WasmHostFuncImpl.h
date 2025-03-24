//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#ifndef RIPPLE_APP_MISC_WASMHOSTFUNCIMPL_H_INLCUDED
#define RIPPLE_APP_MISC_WASMHOSTFUNCIMPL_H_INLCUDED

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/TER.h>

#include "xrpl/basics/base_uint.h"
#include "xrpld/app/misc/WasmVM.h"
#include "xrpld/app/tx/detail/ApplyContext.h"
#include <wasmedge/wasmedge.h>

namespace ripple {
class WasmHostFunctionsImpl : public HostFunctions
{
public:
    WasmHostFunctionsImpl(ApplyContext& ctx, Keylet leKey)
        : ctx(ctx), leKey(leKey)
    {
    }

    int32_t
    getLedgerSqn() override;

    int32_t
    getParentLedgerTime() override;

    std::optional<Bytes>
    getTxField(std::string const& fname) override;

    std::optional<Bytes>
    getLedgerEntryField(
        int32_t type,
        Bytes const& kdata,
        std::string const& fname) override;

    std::optional<Bytes>
    getCurrentLedgerEntryField(std::string const& fname) override;

    bool
    updateData(Bytes const& data) override;

    Hash
    computeSha512HalfHash(Bytes const& data) override;

private:
    ApplyContext& ctx;
    Keylet leKey;
};

}  // namespace ripple
#endif  // RIPPLE_APP_MISC_WASMHOSTFUNCIMPL_H_INLCUDED
