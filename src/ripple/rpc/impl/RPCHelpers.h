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
#include <ripple/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <ripple/protocol/TxMeta.h>

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/Expected.h>
#include <ripple/basics/SubmitSync.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Status.h>
#include <ripple/rpc/impl/Tuning.h>
#include <optional>
#include <variant>

namespace Json {
class Value;
}

namespace ripple {

class ReadView;
class Transaction;

namespace RPC {

struct JsonContext;

/** Get an AccountID from an account ID or public key. */
std::optional<AccountID>
accountFromStringStrict(std::string const&);

// --> strIdent: public key, account ID, or regular seed.
// --> bStrict: Only allow account id or public key.
//
// Returns a Json::objectValue, containing error information if there was one.
Json::Value
accountFromString(
    AccountID& result,
    std::string const& strIdent,
    bool bStrict = false);

/** Decode account ID from string
    @param[out] result account ID decoded from string
    @param strIdent public key, account ID, or regular seed.
    @param bStrict Only allow account id or public key.
    @return code representing error, or rpcSUCCES on success
*/
error_code_i
accountFromStringWithCode(
    AccountID& result,
    std::string const& strIdent,
    bool bStrict = false);

/** Gets the start hint for traversing account objects
 * @param sle - Ledger entry defined by the marker passed into the RPC.
 * @param accountID - The ID of the account whose objects you are traversing.
 */
std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID);

/**
 * Tests if a SLE is owned by accountID.
 * @param ledger - The ledger used to search for the sle.
 * @param sle - The SLE to test for ownership.
 * @param account - The account being tested for SLE ownership.
 */
bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID);

/** Gathers all objects for an account in a ledger.
    @param ledger Ledger to search account objects.
    @param account AccountID to find objects for.
    @param typeFilter Gathers objects of these types. empty gathers all types.
    @param dirIndex Begin gathering account objects from this directory.
    @param entryIndex Begin gathering objects from this directory node.
    @param limit Maximum number of objects to find.
    @param jvResult A JSON result that holds the request objects.
*/
bool
getAccountObjects(
    ReadView const& ledger,
    AccountID const& account,
    std::optional<std::vector<LedgerEntryType>> const& typeFilter,
    uint256 dirIndex,
    uint256 entryIndex,
    std::uint32_t const limit,
    Json::Value& jvResult);

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

bool
isValidated(
    LedgerMaster& ledgerMaster,
    ReadView const& ledger,
    Application& app);

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray);

bool
isHexTxID(std::string const& txid);

/** Inject JSON describing ledger entry

    Effects:
        Adds the JSON description of `sle` to `jv`.

        If `sle` holds an account root, also adds the
        urlgravatar field JSON if sfEmailHash is present.
*/
void
injectSLE(Json::Value& jv, SLE const& sle);

/** Retrieve the limit value from a JsonContext, or set a default -
    then restrict the limit by max and min if not an ADMIN request.

    If there is an error, return it as JSON.
*/
std::optional<Json::Value>
readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const&,
    JsonContext const&);

std::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error);

std::optional<Seed>
parseRippleLibSeed(Json::Value const& params);

/**
 * API version numbers used in API version 1
 */
extern beast::SemanticVersion const firstVersion;
extern beast::SemanticVersion const goodVersion;
extern beast::SemanticVersion const lastVersion;

/**
 * API version numbers used in later API versions
 *
 * Requests with a version number in the range
 * [apiMinimumSupportedVersion, apiMaximumSupportedVersion]
 * are supported.
 *
 * If [beta_rpc_api] is enabled in config, the version numbers
 * in the range [apiMinimumSupportedVersion, apiBetaVersion]
 * are supported.
 *
 * Network Requests without explicit version numbers use
 * apiVersionIfUnspecified. apiVersionIfUnspecified is 1,
 * because all the RPC requests with a version >= 2 must
 * explicitly specify the version in the requests.
 * Note that apiVersionIfUnspecified will be lower than
 * apiMinimumSupportedVersion when we stop supporting API
 * version 1.
 *
 * Command line Requests use apiMaximumSupportedVersion.
 */

constexpr unsigned int apiInvalidVersion = 0;
constexpr unsigned int apiVersionIfUnspecified = 1;
constexpr unsigned int apiMinimumSupportedVersion = 1;
constexpr unsigned int apiMaximumSupportedVersion = 1;
constexpr unsigned int apiBetaVersion = 2;
constexpr unsigned int apiMaximumValidVersion = apiBetaVersion;

static_assert(apiMinimumSupportedVersion >= apiVersionIfUnspecified);
static_assert(apiMaximumSupportedVersion >= apiMinimumSupportedVersion);
static_assert(apiBetaVersion >= apiMaximumSupportedVersion);
static_assert(apiMaximumValidVersion >= apiMaximumSupportedVersion);

template <class Object>
void
setVersion(Object& parent, unsigned int apiVersion, bool betaEnabled)
{
    assert(apiVersion != apiInvalidVersion);
    auto&& object = addObject(parent, jss::version);
    if (apiVersion == apiVersionIfUnspecified)
    {
        object[jss::first] = firstVersion.print();
        object[jss::good] = goodVersion.print();
        object[jss::last] = lastVersion.print();
    }
    else
    {
        object[jss::first] = apiMinimumSupportedVersion;
        object[jss::last] =
            betaEnabled ? apiBetaVersion : apiMaximumSupportedVersion;
    }
}

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params);

/**
 * Retrieve the api version number from the json value
 *
 * Note that APIInvalidVersion will be returned if
 * 1) the version number field has a wrong format
 * 2) the version number retrieved is out of the supported range
 * 3) the version number is unspecified and
 *    APIVersionIfUnspecified is out of the supported range
 *
 * @param value a Json value that may or may not specifies
 *        the api version number
 * @param betaEnabled if the beta API version is enabled
 * @return the api version number
 */
unsigned int
getAPIVersionNumber(const Json::Value& value, bool betaEnabled);

/** Return a ledger based on ledger_hash or ledger_index,
    or an RPC error */
std::variant<std::shared_ptr<Ledger const>, Json::Value>
getLedgerByContext(RPC::JsonContext& context);

std::pair<PublicKey, SecretKey>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion = apiVersionIfUnspecified);
/** Helper to parse submit_mode parameter to RPC submit.
 *
 * @param params RPC parameters
 * @return Either the mode or an error object.
 */
ripple::Expected<RPC::SubmitSync, Json::Value>
getSubmitSyncMode(Json::Value const& params);

}  // namespace RPC
}  // namespace ripple

#endif
