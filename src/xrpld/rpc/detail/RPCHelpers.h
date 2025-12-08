#ifndef XRPL_RPC_RPCHELPERS_H_INCLUDED
#define XRPL_RPC_RPCHELPERS_H_INCLUDED

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/beast/core/SemanticVersion.h>
#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/SecretKey.h>

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

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params);

/**
 * Check if the type is a valid filtering type for account_objects method
 *
 * Since Amendments, DirectoryNode, FeeSettings, LedgerHashes can not be
 * owned by an account, this function will return false in these situations.
 */
bool
isAccountObjectsValidType(LedgerEntryType const& type);

std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion = apiVersionIfUnspecified);

}  // namespace RPC

}  // namespace ripple

#endif
