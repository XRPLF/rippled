//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012=2014 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_RPCHELPERS_H_INCLUDED
#define RIPPLE_RPC_RPCHELPERS_H_INCLUDED

#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/ledger/TxMeta.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/rpc/impl/Tuning.h>
#include <ripple/rpc/Status.h>
#include <boost/optional.hpp>

namespace Json {
class Value;
}

namespace ripple {

class ReadView;
class Transaction;

namespace RPC {

struct Context;

/** Get an AccountID from an account ID or public key. */
boost::optional<AccountID>
accountFromStringStrict (std::string const&);

// --> strIdent: public key, account ID, or regular seed.
// --> bStrict: Only allow account id or public key.
//
// Returns a Json::objectValue, containing error information if there was one.
Json::Value
accountFromString (AccountID& result, std::string const& strIdent,
    bool bStrict = false);

/** Gathers all objects for an account in a ledger.
    @param ledger Ledger to search account objects.
    @param account AccountID to find objects for.
    @param type Gathers objects of this type. ltINVALID gathers all types.
    @param dirIndex Begin gathering account objects from this directory.
    @param entryIndex Begin gathering objects from this directory node.
    @param limit Maximum number of objects to find.
    @param jvResult A JSON result that holds the request objects.
*/
bool
getAccountObjects (ReadView const& ledger, AccountID const& account,
    LedgerEntryType const type, uint256 dirIndex, uint256 const& entryIndex,
    std::uint32_t const limit, Json::Value& jvResult);

/** Look up a ledger from a request and fill a Json::Result with either
    an error, or data representing a ledger.

    If there is no error in the return value, then the ledger pointer will have
    been filled.
*/
Json::Value
lookupLedger (std::shared_ptr<ReadView const>&, Context&);

/** Look up a ledger from a request and fill a Json::Result with the data
    representing a ledger.

    If the returned Status is OK, the ledger pointer will have been filled.
*/
Status
lookupLedger (std::shared_ptr<ReadView const>&, Context&, Json::Value& result);

hash_set <AccountID>
parseAccountIds(Json::Value const& jvArray);

void
addPaymentDeliveredAmount(Json::Value&, Context&,
    std::shared_ptr<Transaction>, TxMeta::pointer);

/** Inject JSON describing ledger entry

    Effects:
        Adds the JSON description of `sle` to `jv`.

        If `sle` holds an account root, also adds the
        urlgravatar field JSON if sfEmailHash is present.
*/
void
injectSLE(Json::Value& jv, SLE const& sle);

/** Retrieve the limit value from a Context, or set a default -
    then restrict the limit by max and min if not an ADMIN request.

    If there is an error, return it as JSON.
*/
boost::optional<Json::Value>
readLimitField(unsigned int& limit, Tuning::LimitRange const&, Context const&);

boost::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error);

boost::optional<Seed>
parseRippleLibSeed(Json::Value const& params);

std::pair<PublicKey, SecretKey>
keypairForSignature(Json::Value const& params, Json::Value& error);

extern beast::SemanticVersion const firstVersion;
extern beast::SemanticVersion const goodVersion;
extern beast::SemanticVersion const lastVersion;

template <class Object>
void
setVersion(Object& parent)
{
    auto&& object = addObject (parent, jss::version);
    object[jss::first] = firstVersion.print();
    object[jss::good] = goodVersion.print();
    object[jss::last] = lastVersion.print();
}

std::pair<RPC::Status, LedgerEntryType>
    chooseLedgerEntryType(Json::Value const& params);

} // RPC
} // ripple

#endif
