//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#pragma once

#include <xrpld/app/wasm/ParamsHelper.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

enum class HostFunctionError : int32_t {
    INTERNAL = -1,
    FIELD_NOT_FOUND = -2,
    BUFFER_TOO_SMALL = -3,
    NO_ARRAY = -4,
    NOT_LEAF_FIELD = -5,
    LOCATOR_MALFORMED = -6,
    SLOT_OUT_RANGE = -7,
    SLOTS_FULL = -8,
    EMPTY_SLOT = -9,
    LEDGER_OBJ_NOT_FOUND = -10,
    DECODING = -11,
    DATA_FIELD_TOO_LARGE = -12,
    POINTER_OUT_OF_BOUNDS = -13,
    NO_MEM_EXPORTED = -14,
    INVALID_PARAMS = -15,
    INVALID_ACCOUNT = -16,
    INVALID_FIELD = -17,
    INDEX_OUT_OF_BOUNDS = -18,
    FLOAT_INPUT_MALFORMED = -19,
    FLOAT_COMPUTATION_ERROR = -20,
};

struct HostFunctions
{
    // LCOV_EXCL_START
    virtual void
    setRT(void const*)
    {
    }

    virtual void const*
    getRT() const
    {
        return nullptr;
    }

    virtual beast::Journal
    getJournal()
    {
        return beast::Journal{beast::Journal::getNullSink()};
    }

    virtual Expected<std::int32_t, HostFunctionError>
    getLedgerSqn()
    {
        return Unexpected(HostFunctionError::INTERNAL);
    }

    virtual ~HostFunctions() = default;
    // LCOV_EXCL_STOP
};

}  // namespace ripple
