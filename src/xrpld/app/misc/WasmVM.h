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
#ifndef RIPPLE_APP_MISC_WASMVM_H_INLCUDED
#define RIPPLE_APP_MISC_WASMVM_H_INLCUDED

#include <xrpl/basics/Expected.h>
// #include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>

#include <wasmedge/wasmedge.h>

namespace ripple {

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    int32_t input);

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& accountID);

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& escrow_tx_json_data,
    std::vector<uint8_t> const& escrow_lo_json_data);

Expected<std::pair<bool, std::string>, TER>
runEscrowWasmP4(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    std::vector<uint8_t> const& escrow_tx_json_data,
    std::vector<uint8_t> const& escrow_lo_json_data);

struct LedgerDataProvider
{
    virtual int32_t
    get_ledger_sqn()
    {
        return 1;
    }

    virtual ~LedgerDataProvider() = default;
};

Expected<bool, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    LedgerDataProvider* ledgerDataProvider);

}  // namespace ripple
#endif  // RIPPLE_APP_MISC_WASMVM_H_INLCUDED

// class WasmVM final
//{
// public:
//     explicit WasmVM(beast::Journal j);
//     ~WasmVM() = default;
//
// private:
//     beast::Journal j_;
// };