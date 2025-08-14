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

#ifndef RIPPLE_RPC_RPCLEDGERHELPERS_H_INCLUDED
#define RIPPLE_RPC_RPCLEDGERHELPERS_H_INCLUDED

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>

#include <optional>
namespace ripple {

class ReadView;
class Transaction;

namespace RPC {

struct JsonContext;

/** Get ledger by hash
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context& context);

/** Get ledger by sequence
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context& context);

enum LedgerShortcut { CURRENT, CLOSED, VALIDATED };
/** Get ledger specified in shortcut.
    If there is no error in the return value, the ledger pointer will have
    been filled
*/
template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context& context);

/** Look up a ledger from a request and fill a Json::Result with either
    an error, or data representing a ledger.

    If there is no error in the return value, then the ledger pointer will have
    been filled.
*/
Json::Value
lookupLedger(std::shared_ptr<ReadView const>&, JsonContext&);

/** Look up a ledger from a request and fill a Json::Result with the data
    representing a ledger.

    If the returned Status is OK, the ledger pointer will have been filled.
*/
Status
lookupLedger(
    std::shared_ptr<ReadView const>&,
    JsonContext&,
    Json::Value& result);

template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R>& context);

template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context& context);

/**
 * @brief Retrieves or acquires a ledger based on the parameters provided in the
 * given JsonContext.
 *
 * This function differs from the other ledger getter functions in this file in
 * that it attempts to either retrieve an existing ledger or acquire it if it is
 * not already available, based on the context of the RPC request. It returns an
 * Expected containing either a shared pointer to the requested immutable Ledger
 * object or a Json::Value describing an error. Unlike the other getLedger or
 * lookupLedger functions, which typically fill a provided ledger pointer or
 * result object and return a Status, this function encapsulates both the result
 * and error in a single return value, making it easier to handle success and
 * failure cases in a unified way.
 *
 * @param context The RPC JsonContext containing request parameters and
 * environment.
 * @return Expected<std::shared_ptr<Ledger const>, Json::Value>
 *         On success, contains a shared pointer to the requested Ledger.
 *         On failure, contains a Json::Value describing the error.
 */
Expected<std::shared_ptr<Ledger const>, Json::Value>
getOrAcquireLedger(RPC::JsonContext& context);

}  // namespace RPC

}  // namespace ripple

#endif
