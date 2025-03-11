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

#include "xrpl/basics/base_uint.h"
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

struct EscrowResultP6
{
    bool result;
    int32_t cost;
};

Expected<EscrowResultP6, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    int32_t gasLimit,
    int32_t input);

/////////////// Devnet 1 /////////////////
typedef std::vector<uint8_t> Bytes;
typedef ripple::uint256 Hash;

template <typename T>
struct WasmResult
{
    T result;
    uint64_t cost;
};
typedef WasmResult<bool> EscrowResult;

struct HostFunctions{
    virtual int32_t
    getLedgerSqn()
    {
        return 1;
    }

    virtual int32_t
    getParentLedgerTime()
    {
        return 1;
    }

    virtual std::optional<Bytes>
    getTxField(std::string const& fname)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    getLedgerEntryField(
        int32_t type,
        Bytes const& kdata,
        std::string const& fname)
    {
        return Bytes{};
    }

    virtual std::optional<Bytes>
    getCurrentLedgerEntryField(std::string const& fname)
    {
        return Bytes{};
    }

    virtual bool
    updateData(Bytes const& data)
    {
        return true;
    }

    virtual Hash
    computeSha512HalfHash(Bytes const& data){
        return Hash {};
    }

    virtual ~HostFunctions() = default;
};

Expected<EscrowResult, TER>
runEscrowWasm(
    std::vector<uint8_t> const& wasmCode,
    std::string const& funcName,
    HostFunctions* hfs,
    uint64_t gasLimit);

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


