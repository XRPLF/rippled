#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/jss.h>

#include <org/xrpl/rpc/v1/ledger.pb.h>

#include <cstdint>
#include <memory>

namespace xrpl::RPC {

namespace {

/** Return true if the node's validated ledger is too stale to trust.
 *
 *  Compares `LedgerMaster::getValidatedLedgerAge()` against
 *  `Tuning::kMAX_VALIDATED_LEDGER_AGE` (2 minutes). Always returns `false`
 *  in standalone mode, where there are no peers and network-dependent
 *  staleness is meaningless.
 *
 *  @param ledgerMaster The ledger master to query for age.
 *  @param standalone   True if the node is running in standalone mode.
 *  @return `true` if validation is stale and the node should refuse to serve
 *      ledger data; `false` if the node is current or in standalone mode.
 */
bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge() > Tuning::kMAX_VALIDATED_LEDGER_AGE;
}

/** Resolve a ledger from a JSON hex-hash value.
 *
 *  Parses `hash` as a `uint256` hex string and delegates to
 *  `getLedger(ledger, hash, context)`. There is no staleness check: the
 *  caller named an immutable object by identity, so "too old" does not apply.
 *
 *  @tparam T        Ledger pointer type (e.g., `std::shared_ptr<ReadView const>`).
 *  @param ledger    Output; set on success.
 *  @param hash      JSON value expected to contain a 64-hex-character string.
 *  @param context   RPC context providing `LedgerMaster` and config.
 *  @param fieldName Field name used in error messages (e.g., `jss::ledger_hash`).
 *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` if the string is not
 *      valid hex; `rpcLGR_NOT_FOUND` if no ledger with that hash is cached.
 */
template <class T>
Status
ledgerFromHash(
    T& ledger,
    json::Value hash,
    Context const& context,
    json::StaticString const fieldName)
{
    uint256 ledgerHash;
    if (!ledgerHash.parseHex(hash.asString()))
        return {RpcInvalidParams, expectedFieldMessage(fieldName, "hex string")};
    return getLedger(ledger, ledgerHash, context);
}

/** Resolve a ledger from a JSON index or shortcut value.
 *
 *  Accepts the canonical shortcut strings `"current"` (also the empty string),
 *  `"validated"`, and `"closed"`, delegating each to the corresponding
 *  `LedgerShortcut` overload of `getLedger()`. Any other string value is
 *  parsed as a decimal `uint32_t` sequence number via `beast::lexicalCastChecked`.
 *
 *  @tparam T          Ledger pointer type.
 *  @param ledger      Output; set on success.
 *  @param indexValue  JSON value containing a shortcut string or integer index.
 *  @param context     RPC context providing `LedgerMaster` and config.
 *  @param fieldName   Field name used in error messages (e.g., `jss::ledger_index`).
 *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` if the value cannot
 *      be interpreted; network/sync errors propagated from `getLedger()`.
 */
template <class T>
Status
ledgerFromIndex(
    T& ledger,
    json::Value indexValue,
    Context const& context,
    json::StaticString const fieldName)
{
    auto const index = indexValue.asString();

    if (index == "current" || index.empty())
        return getLedger(ledger, LedgerShortcut::Current, context);

    if (index == "validated")
        return getLedger(ledger, LedgerShortcut::Validated, context);

    if (index == "closed")
        return getLedger(ledger, LedgerShortcut::Closed, context);

    std::uint32_t iVal = 0;
    if (!beast::lexicalCastChecked(iVal, index))
        return {RpcInvalidParams, expectedFieldMessage(fieldName, "string or number")};

    return getLedger(ledger, iVal, context);
}

/** Resolve a ledger from a JSON-RPC request's parameter object.
 *
 *  Enforces mutual exclusivity: at most one of `ledger`, `ledger_hash`, or
 *  `ledger_index` may be present. Supplying more than one returns
 *  `rpcINVALID_PARAMS`. The deprecated `ledger` field is still accepted but
 *  is not mentioned in the error message when only `ledger_hash` and
 *  `ledger_index` conflict, nudging callers toward the non-deprecated API.
 *
 *  The legacy `ledger` field uses a length heuristic: a string of exactly 64
 *  characters is treated as a hash; anything else is treated as an index or
 *  shortcut. If neither `ledger_hash` nor `ledger_index` is present, defaults
 *  to `LedgerShortcut::Current`.
 *
 *  @tparam T      Ledger pointer type.
 *  @param ledger  Output; reset on entry, set on success.
 *  @param context JSON-RPC context whose `params` supply the ledger selector.
 *  @return `Status::kOK` on success; `rpcINVALID_PARAMS` for malformed or
 *      conflicting fields; network/sync errors from `getLedger()`.
 */
template <class T>
Status
ledgerFromRequest(T& ledger, JsonContext const& context)
{
    ledger.reset();

    auto& params = context.params;
    auto const hasLedger = context.params.isMember(jss::ledger);
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);

    if ((hasLedger + hasHash + hasIndex) > 1)
    {
        // while `ledger` is still supported, it is deprecated
        // and therefore shouldn't be mentioned in the error message
        if (hasLedger)
        {
            return {
                RpcInvalidParams,
                "Exactly one of 'ledger', 'ledger_hash', or "
                "'ledger_index' can be specified."};
        }
        return {
            RpcInvalidParams,
            "Exactly one of 'ledger_hash' or "
            "'ledger_index' can be specified."};
    }

    if (hasLedger)
    {
        auto& legacyLedger = params[jss::ledger];
        if (!legacyLedger.isString() && !legacyLedger.isUInt() && !legacyLedger.isInt())
        {
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger, "string or number")};
        }
        if (legacyLedger.isString() && legacyLedger.asString().size() == 64)
        {
            return ledgerFromHash(ledger, legacyLedger, context, jss::ledger);
        }

        return ledgerFromIndex(ledger, legacyLedger, context, jss::ledger);
    }

    if (hasHash)
    {
        auto const& ledgerHash = params[jss::ledger_hash];
        if (!ledgerHash.isString())
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger_hash, "hex string")};
        return ledgerFromHash(ledger, ledgerHash, context, jss::ledger_hash);
    }

    if (hasIndex)
    {
        auto const& ledgerIndex = params[jss::ledger_index];
        if (!ledgerIndex.isString() && !ledgerIndex.isUInt() && !ledgerIndex.isInt())
        {
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger_index, "string or number")};
        }
        return ledgerFromIndex(ledger, ledgerIndex, context, jss::ledger_index);
    }

    return getLedger(ledger, LedgerShortcut::Current, context);
}
}  // namespace

/** Resolve a ledger from a gRPC request context.
 *
 *  Extracts the `LedgerSpecifier` protobuf field from the request and
 *  delegates to `ledgerFromSpecifier()`. Explicitly instantiated for
 *  `GetLedgerEntryRequest`, `GetLedgerDataRequest`, and `GetLedgerRequest`.
 *
 *  @tparam T       Ledger pointer type.
 *  @tparam R       gRPC request type; must expose a `.ledger()` method
 *      returning a `LedgerSpecifier`.
 *  @param ledger   Output; set on success.
 *  @param context  gRPC context whose `params` provide the request.
 *  @return `Status::kOK` on success; errors propagated from
 *      `ledgerFromSpecifier()`.
 */
template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R> const& context)
{
    R const& request = context.params;
    return ledgerFromSpecifier(ledger, request.ledger(), context);
}

template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest> const&);

template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest> const&);

template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest> const&);

/** Resolve a ledger from a protobuf `LedgerSpecifier`.
 *
 *  Maps each `LedgerCase` variant to the appropriate `getLedger()` overload:
 *  - `kHash` → lookup by `uint256` (returns `rpcINVALID_PARAMS` if malformed).
 *  - `kSequence` → lookup by sequence number.
 *  - `kShortcut` / `LEDGER_NOT_SET` → maps `SHORTCUT_VALIDATED`,
 *    `SHORTCUT_CURRENT`, `SHORTCUT_UNSPECIFIED`, and `SHORTCUT_CLOSED` to their
 *    `LedgerShortcut` equivalents. An unrecognised shortcut falls through to
 *    `Status::kOK` with `ledger` left in its reset state.
 *
 *  @tparam T         Ledger pointer type.
 *  @param ledger     Output; reset on entry, set on success.
 *  @param specifier  The protobuf specifier describing the desired ledger.
 *  @param context    RPC context providing `LedgerMaster` and config.
 *  @return `Status::kOK` on success; errors propagated from `getLedger()`.
 */
template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context const& context)
{
    ledger.reset();

    using LedgerCase = org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase;
    LedgerCase const ledgerCase = specifier.ledger_case();
    switch (ledgerCase)
    {
        case LedgerCase::kHash: {
            if (auto hash = uint256::fromVoidChecked(specifier.hash()))
            {
                return getLedger(ledger, *hash, context);
            }
            return {RpcInvalidParams, "ledgerHashMalformed"};
        }
        case LedgerCase::kSequence:
            return getLedger(ledger, specifier.sequence(), context);
        case LedgerCase::kShortcut:
            [[fallthrough]];
        case LedgerCase::LEDGER_NOT_SET: {
            auto const shortcut = specifier.shortcut();
            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED)
            {
                return getLedger(ledger, LedgerShortcut::Validated, context);
            }

            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT ||
                shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED)
            {
                return getLedger(ledger, LedgerShortcut::Current, context);
            }
            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED)
            {
                return getLedger(ledger, LedgerShortcut::Closed, context);
            }
        }
    }

    return Status::kOK;
}

/** Retrieve a ledger by its hash, with no staleness check.
 *
 *  The caller named an immutable object by identity; there is no concept of
 *  "too old" for a specific historical ledger, so `isValidatedOld()` is
 *  deliberately not called here.
 *
 *  @tparam T        Ledger pointer type.
 *  @param ledger    Output; set on success.
 *  @param ledgerHash The 256-bit hash of the desired ledger.
 *  @param context   RPC context providing `LedgerMaster`.
 *  @return `Status::kOK` if the ledger is in cache;
 *      `rpcLGR_NOT_FOUND` otherwise.
 */
template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context const& context)
{
    ledger = context.ledgerMaster.getLedgerByHash(ledgerHash);
    if (ledger == nullptr)
        return {RpcLgrNotFound, "ledgerNotFound"};
    return Status::kOK;
}

/** Retrieve a ledger by sequence number with staleness guard.
 *
 *  Falls back to the current open ledger when `getLedgerBySeq()` returns
 *  null, handling the common case where the requested sequence is the ledger
 *  still being built. After lookup, if the resolved ledger's sequence exceeds
 *  the last validated index *and* `isValidatedOld()` is true, the node is
 *  considered out-of-sync and an error is returned instead of stale data.
 *
 *  @tparam T            Ledger pointer type.
 *  @param ledger        Output; set on success, reset on sync error.
 *  @param ledgerIndex   Sequence number of the desired ledger.
 *  @param context       RPC context providing `LedgerMaster` and config.
 *  @return `Status::kOK` on success; `rpcLGR_NOT_FOUND` if not cached;
 *      `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+) if the node
 *      is stale and the ledger is ahead of the validated chain.
 */
template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context const& context)
{
    ledger = context.ledgerMaster.getLedgerBySeq(ledgerIndex);
    if (ledger == nullptr)
    {
        auto cur = context.ledgerMaster.getCurrentLedger();
        if (cur->header().seq == ledgerIndex)
        {
            ledger = cur;
        }
    }

    if (ledger == nullptr)
        return {RpcLgrNotFound, "ledgerNotFound"};

    if (ledger->header().seq > context.ledgerMaster.getValidLedgerIndex() &&
        isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        ledger.reset();
        if (context.apiVersion == 1)
            return {RpcNoNetwork, "InsufficientNetworkMode"};
        return {RpcNotSynced, "notSynced"};
    }

    return Status::kOK;
}

/** Retrieve a ledger by shortcut (`Current`, `Closed`, or `Validated`).
 *
 *  Performs the staleness check *before* lookup — a stale node returns an
 *  error regardless of which shortcut was requested. For `Validated`, an
 *  additional null check guards against the race where no ledger has been
 *  validated yet. For `Current` and `Closed`, a sequence-gap check rejects
 *  responses when the shortcut ledger is more than 10 sequences behind the
 *  last validated index, protecting callers from data on a clearly-behind node.
 *
 *  @tparam T         Ledger pointer type.
 *  @param ledger     Output; set on success, reset on error.
 *  @param shortcut   Which ledger to retrieve (`Current`, `Closed`,
 *      or `Validated`).
 *  @param context    RPC context providing `LedgerMaster` and config.
 *  @return `Status::kOK` on success;
 *      `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+) if stale or
 *      the sequence gap exceeds 10; `rpcINVALID_PARAMS` for an unrecognised
 *      shortcut value.
 *  @note The sequence-gap threshold of 10 is a hard-coded heuristic.
 *  @note `Current` returns an open ledger; `Closed` and `Validated` return
 *      closed ledgers — violations cause `XRPL_ASSERT` failures.
 */
template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context const& context)
{
    if (isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        if (context.apiVersion == 1)
            return {RpcNoNetwork, "InsufficientNetworkMode"};
        return {RpcNotSynced, "notSynced"};
    }

    if (shortcut == LedgerShortcut::Validated)
    {
        ledger = context.ledgerMaster.getValidatedLedger();
        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }

        XRPL_ASSERT(!ledger->open(), "xrpl::RPC::getLedger : validated is not open");
    }
    else
    {
        if (shortcut == LedgerShortcut::Current)
        {
            ledger = context.ledgerMaster.getCurrentLedger();
            XRPL_ASSERT(ledger->open(), "xrpl::RPC::getLedger : current is open");
        }
        else if (shortcut == LedgerShortcut::Closed)
        {
            ledger = context.ledgerMaster.getClosedLedger();
            XRPL_ASSERT(!ledger->open(), "xrpl::RPC::getLedger : closed is not open");
        }
        else
        {
            return {RpcInvalidParams, "ledgerIndexMalformed"};
        }

        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }

        static auto const kMIN_SEQUENCE_GAP = 10;

        if (ledger->header().seq + kMIN_SEQUENCE_GAP < context.ledgerMaster.getValidLedgerIndex())
        {
            ledger.reset();
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }
    }
    return Status::kOK;
}

template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint32_t, Context const&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, LedgerShortcut shortcut, Context const&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint256 const&, Context const&);

/** Resolve a ledger and populate the JSON result with identifying fields.
 *
 *  After calling `ledgerFromRequest()`, writes the identifying fields into
 *  `result`: closed ledgers receive `ledger_hash` and `ledger_index`; the
 *  open (current) ledger receives only `ledger_current_index`. The `validated`
 *  boolean is always written. If `ledger_index` is absent, defaults to the
 *  current open ledger.
 *
 *  @param ledger   Output; set on success.
 *  @param context  JSON-RPC context whose `params` supply the ledger selector.
 *  @param result   JSON object to populate; must be an object type. On error,
 *      the caller should check the returned `Status` — `result` is not
 *      modified on failure.
 *  @return `Status::kOK` on success; errors from `ledgerFromRequest()`.
 */
Status
lookupLedger(
    std::shared_ptr<ReadView const>& ledger,
    JsonContext const& context,
    json::Value& result)
{
    if (auto status = ledgerFromRequest(ledger, context))
        return status;

    auto& info = ledger->header();

    if (!ledger->open())
    {
        result[jss::ledger_hash] = to_string(info.hash);
        result[jss::ledger_index] = info.seq;
    }
    else
    {
        result[jss::ledger_current_index] = info.seq;
    }

    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);
    return Status::kOK;
}

/** Resolve a ledger and return the JSON result object directly.
 *
 *  Convenience wrapper around the three-argument overload. On error, the
 *  `Status` is injected into the returned JSON object rather than returned
 *  separately, giving callers a single return value to check and forward.
 *
 *  @param ledger   Output; set on success, unmodified on error.
 *  @param context  JSON-RPC context whose `params` supply the ledger selector.
 *  @return A `json::Value` object containing ledger identifiers on success,
 *      or an error description with the injected status on failure.
 */
json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext const& context)
{
    json::Value result;
    if (auto status = lookupLedger(ledger, context, result))
        status.inject(result);

    return result;
}

/** Retrieve a ledger by hash or index, triggering a network fetch if needed.
 *
 *  Used exclusively by the `ledger_request` admin command. Unlike the other
 *  ledger helpers, this function can initiate an active acquisition via
 *  `InboundLedgers::acquire()` when the ledger is not locally cached.
 *
 *  Exactly one of `ledger_hash` or `ledger_index` must be supplied; the
 *  deprecated `ledger` field is not supported. The sequence-index path
 *  uses a two-step skip-list walk: it first tries `hashOfSeq()` against the
 *  validated ledger directly, and if the skip list doesn't reach far enough
 *  back, it computes a candidate boundary ledger via `getCandidateLedger()`,
 *  acquires that ledger, and then resolves the target hash from its skip list.
 *
 *  If acquisition is in progress rather than complete, the function returns a
 *  JSON error object describing the pending fetch — this is a polling model;
 *  the caller should retry. In standalone mode the network path is bypassed
 *  and the ledger is retrieved from the cache only.
 *
 *  @param context  JSON-RPC context; must contain exactly one of
 *      `ledger_hash` (hex string) or `ledger_index` (positive integer).
 *  @return On success, a `shared_ptr<Ledger const>` for the resolved ledger.
 *      On failure, a `json::Value` describing the error, which may include
 *      an `"acquiring"` field with in-progress acquisition status.
 *  @note The sequence-index path validates that the node's ledger is not stale
 *      before attempting the skip-list lookup.
 */
Expected<std::shared_ptr<Ledger const>, json::Value>
getOrAcquireLedger(RPC::JsonContext const& context)
{
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);
    std::uint32_t ledgerIndex = 0;

    auto& ledgerMaster = context.app.getLedgerMaster();
    LedgerHash ledgerHash;

    if ((static_cast<int>(hasHash) + static_cast<int>(hasIndex)) != 1)
    {
        return Unexpected(
            RPC::makeParamError(
                "Exactly one of 'ledger_hash' or "
                "'ledger_index' can be specified."));
    }

    if (hasHash)
    {
        auto const& jsonHash = context.params.get(jss::ledger_hash, json::ValueType::Null);
        if (!jsonHash.isString() || !ledgerHash.parseHex(jsonHash.asString()))
            return Unexpected(RPC::expectedFieldError(jss::ledger_hash, "hex string"));
    }
    else
    {
        auto const& jsonIndex = context.params.get(jss::ledger_index, json::ValueType::Null);
        if (!jsonIndex.isInt() && !jsonIndex.isUInt())
            return Unexpected(RPC::expectedFieldError(jss::ledger_index, "number"));

        if (ledgerMaster.getValidatedLedgerAge() > RPC::Tuning::kMAX_VALIDATED_LEDGER_AGE)
        {
            if (context.apiVersion == 1)
                return Unexpected(rpcError(RpcNoCurrent));
            return Unexpected(rpcError(RpcNotSynced));
        }

        ledgerIndex = jsonIndex.asInt();
        auto ledger = ledgerMaster.getValidatedLedger();

        if (ledgerIndex >= ledger->header().seq)
            return Unexpected(RPC::makeParamError("Ledger index too large"));
        if (ledgerIndex <= 0)
            return Unexpected(RPC::makeParamError("Ledger index too small"));

        auto const j = context.app.getJournal("RPCHandler");
        auto neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        if (!neededHash)
        {
            auto const refIndex = getCandidateLedger(ledgerIndex);
            auto refHash = hashOfSeq(*ledger, refIndex, j);
            XRPL_ASSERT(refHash, "xrpl::RPC::getOrAcquireLedger : nonzero ledger hash");

            // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above
            ledger = ledgerMaster.getLedgerByHash(*refHash);
            if (!ledger)
            {
                if (auto il = context.app.getInboundLedgers().acquire(
                        *refHash, refIndex, InboundLedger::Reason::GENERIC))
                {
                    json::Value jvResult = RPC::makeError(
                        RpcLgrNotFound, "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = getJson(LedgerFill(*il, &context));
                    return Unexpected(jvResult);
                }

                if (auto il = context.app.getInboundLedgers().find(*refHash))
                // NOLINTEND(bugprone-unchecked-optional-access)
                {
                    json::Value jvResult = RPC::makeError(
                        RpcLgrNotFound, "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = il->getJson(0);
                    return Unexpected(jvResult);
                }

                // Likely the app is shutting down
                return Unexpected(json::Value());
            }

            neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        }
        XRPL_ASSERT(neededHash, "xrpl::RPC::getOrAcquireLedger : nonzero needed hash");
        ledgerHash = neededHash ? *neededHash : beast::kZERO;  // kludge
    }

    auto ledger = context.app.getInboundLedgers().acquire(
        ledgerHash, ledgerIndex, InboundLedger::Reason::GENERIC);

    // In standalone mode there are no peers to fetch from; fall back to cache.
    if (!ledger && context.app.config().standalone())
        ledger = ledgerMaster.getLedgerByHash(ledgerHash);

    if (ledger)
        return ledger;

    if (auto il = context.app.getInboundLedgers().find(ledgerHash))
        return Unexpected(il->getJson(0));

    return Unexpected(RPC::makeError(RpcNotReady, "findCreate failed to return an inbound ledger"));
}

}  // namespace xrpl::RPC
