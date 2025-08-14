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

enum LedgerShortcut { CURRENT, CLOSED, VALIDATED };

/**
 * @brief Retrieves a ledger by its hash.
 *
 * This function attempts to find and fill the provided ledger pointer with the
 * ledger corresponding to the specified hash. If the operation is successful,
 * the ledger pointer will be set; otherwise, an error status is returned.
 *
 * @tparam T Type of the ledger pointer to be filled.
 * @param ledger Reference to the ledger pointer to be filled.
 * @param ledgerHash The hash of the ledger to retrieve.
 * @param context The RPC context containing request parameters and environment.
 * @return Status indicating success or failure of the operation.
 */
template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context& context);

/**
 * @brief Retrieves a ledger by its sequence index.
 *
 * This function attempts to find and fill the provided ledger pointer with the
 * ledger corresponding to the specified sequence index. If the operation is
 * successful, the ledger pointer will be set; otherwise, an error status is
 * returned.
 *
 * @tparam T Type of the ledger pointer to be filled.
 * @param ledger Reference to the ledger pointer to be filled.
 * @param ledgerIndex The sequence index of the ledger to retrieve.
 * @param context The RPC context containing request parameters and environment.
 * @return Status indicating success or failure of the operation.
 */
template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context& context);

/**
 * @brief Retrieves a ledger specified by a `LedgerShortcut`.
 *
 * This function attempts to find and fill the provided ledger pointer with the
 * ledger corresponding to the specified shortcut. If the operation is
 * successful, the ledger pointer will be set; otherwise, an error status is
 * returned.
 *
 * @tparam T Type of the ledger pointer to be filled.
 * @param ledger Reference to the ledger pointer to be filled.
 * @param shortcut The shortcut specifying which ledger to retrieve.
 * @param context The RPC context containing request parameters and environment.
 * @return Status indicating success or failure of the operation.
 */
template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context& context);

/**
 * @brief Looks up a ledger from a request and returns a Json::Value with either
 * an error or ledger data.
 *
 * This function attempts to find a ledger based on the parameters in the given
 * JsonContext. On success, the ledger pointer is filled and a Json::Value
 * representing the ledger is returned. On failure, a Json::Value describing the
 * error is returned.
 *
 * @param ledger Reference to a shared pointer to the ledger to be filled.
 * @param context The RPC JsonContext containing request parameters and
 * environment.
 * @return Json::Value containing either the ledger data or an error
 * description.
 */
Json::Value
lookupLedger(std::shared_ptr<ReadView const>&, JsonContext&);

/**
 * @brief Looks up a ledger from a request and fills a Json::Value with ledger
 * data.
 *
 * This function attempts to find a ledger based on the parameters in the given
 * JsonContext. On success, the ledger pointer is filled and the result
 * parameter is populated with ledger data. On failure, an error status is
 * returned.
 *
 * @param ledger Reference to a shared pointer to the ledger to be filled.
 * @param context The RPC JsonContext containing request parameters and
 * environment.
 * @param result Reference to a Json::Value to be filled with ledger data.
 * @return Status indicating success or failure of the operation.
 */
Status
lookupLedger(
    std::shared_ptr<ReadView const>&,
    JsonContext&,
    Json::Value& result);

/**
 * @brief Retrieves a ledger from a gRPC request context.
 *
 * This function attempts to find and fill the provided ledger pointer based on
 * the parameters in the given gRPC context. On success, the ledger pointer is
 * filled.
 *
 * @tparam T Type of the ledger pointer to be filled.
 * @tparam R Type of the gRPC request.
 * @param ledger Reference to the ledger pointer to be filled.
 * @param context The gRPC context containing request parameters and
 * environment.
 * @return Status indicating success or failure of the operation.
 */
template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R>& context);

/**
 * @brief Retrieves a ledger based on a LedgerSpecifier.
 *
 * This function attempts to find and fill the provided ledger pointer based on
 * the specified LedgerSpecifier. On success, the ledger pointer is filled.
 *
 * @tparam T Type of the ledger pointer to be filled.
 * @param ledger Reference to the ledger pointer to be filled.
 * @param specifier The LedgerSpecifier describing which ledger to retrieve.
 * @param context The RPC context containing request parameters and environment.
 * @return Status indicating success or failure of the operation.
 */
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
