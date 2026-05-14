/** @file
 *  Server-side pipeline for the four XRPL RPC signing operations:
 *  `sign`, `submit`, `sign_for`, and `submit_multisigned`.
 *
 *  The file converts raw JSON arriving over an RPC connection into a fully
 *  validated, signed, serialized `STTx` and hands it off to
 *  `NetworkOPs::processTransaction`.  Nothing here touches consensus or the
 *  ledger engine directly.  Single-key and multi-party (threshold) signing
 *  share the same pre-processing path, unified through `SigningForParams`.
 *
 *  Public API surface is declared in `TransactionSign.h`.
 */
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/AssetCache.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/Pathfinder.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/apply.h>  // Validity::Valid
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace xrpl::RPC {
namespace detail {

/** Mode discriminator and accumulator for the signing pipeline.
 *
 *  `transactionPreProcessImpl` handles both single-signing and multi-signing
 *  without code duplication.  `SigningForParams` carries the per-call context
 *  that distinguishes the two modes and accumulates the multi-signature
 *  produced during the pre-processing pass.
 *
 *  Mode is encoded as the nullness of `multiSigningAcctID_`:
 *  - A null pointer means single-signing (default-constructed).
 *  - A non-null pointer means multi-signing; the pointed-to value is
 *    always owned by the caller's stack frame, so the raw pointer is safe
 *    for the lifetime of the call chain.
 *
 *  Copy is deleted to prevent accidental escape from the local call chain.
 *  `getSigner()` and `getPublicKey()` call `logicError()` rather than
 *  dereferencing unsafely when invoked in the wrong mode.
 */
class SigningForParams
{
private:
    /** Non-null iff multi-signing; points into the caller's stack frame. */
    AccountID const* const multiSigningAcctID_;
    /** Public key resolved during pre-processing; populated only in multi-signing mode. */
    std::optional<PublicKey> multiSignPublicKey_;
    /** Computed multi-signature; populated by `transactionPreProcessImpl`. */
    Buffer multiSignature_;
    /** Optional SField reference that routes the signature into a nested inner
     *  object instead of the transaction root (`signature_target` RPC parameter). */
    std::optional<std::reference_wrapper<SField const>> signatureTarget_;

public:
    /** Construct in single-signing mode. */
    explicit SigningForParams() : multiSigningAcctID_(nullptr)
    {
    }

    SigningForParams(SigningForParams const& rhs) = delete;

    /** Construct in multi-signing mode for the given signer account.
     *
     *  @param multiSigningAcctID  The `AccountID` of the signing party.  Must
     *      outlive this object (typically it lives on the caller's stack).
     */
    SigningForParams(AccountID const& multiSigningAcctID) : multiSigningAcctID_(&multiSigningAcctID)
    {
    }

    /** Return true when constructed for multi-signing. */
    [[nodiscard]] bool
    isMultiSigning() const
    {
        return multiSigningAcctID_ != nullptr;
    }

    /** Return true when constructed for single-signing. */
    [[nodiscard]] bool
    isSingleSigning() const
    {
        return !isMultiSigning();
    }

    /** Return true when `tx_json` fields may be auto-filled.
     *
     *  Multi-signing skips auto-fill because the transaction must already be
     *  fully formed before signers contribute their signatures.
     */
    [[nodiscard]] bool
    editFields() const
    {
        return !isMultiSigning();
    }

    /** Return true when a complete multi-signature has been accumulated.
     *
     *  Valid only after `transactionPreProcessImpl` has run in multi-signing
     *  mode.
     */
    [[nodiscard]] bool
    validMultiSign() const
    {
        return isMultiSigning() && multiSignPublicKey_ && !multiSignature_.empty();
    }

    /** Return the multi-signing account ID.
     *
     *  @pre `isMultiSigning()` must be true; calls `logicError()` otherwise.
     */
    [[nodiscard]] AccountID const&
    getSigner() const
    {
        if (multiSigningAcctID_ == nullptr)
            logicError("Accessing unknown SigningForParams::getSigner()");
        return *multiSigningAcctID_;
    }

    /** Return the resolved public key.
     *
     *  @pre `setPublicKey()` must have been called; calls `logicError()` otherwise.
     */
    [[nodiscard]] PublicKey const&
    getPublicKey() const
    {
        if (!multiSignPublicKey_)
            logicError("Accessing unknown SigningForParams::getPublicKey()");
        return *multiSignPublicKey_;
    }

    /** Return the computed multi-signature buffer.
     *
     *  Empty until `moveMultiSignature()` has been called.
     */
    [[nodiscard]] Buffer const&
    getSignature() const
    {
        return multiSignature_;
    }

    /** Return the optional signature-target SField.
     *
     *  When set, the signature is routed into the named inner object rather
     *  than the transaction root.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<SField const>> const&
    getSignatureTarget() const
    {
        return signatureTarget_;
    }

    /** Record the resolved public key (multi-signing mode only). */
    void
    setPublicKey(PublicKey const& multiSignPublicKey)
    {
        multiSignPublicKey_ = multiSignPublicKey;
    }

    /** Set the optional signature-target field reference. */
    void
    setSignatureTarget(std::optional<std::reference_wrapper<SField const>> const& field)
    {
        signatureTarget_ = field;
    }

    /** Move-store the computed multi-signature produced by signing. */
    void
    moveMultiSignature(Buffer&& multiSignature)
    {
        multiSignature_ = std::move(multiSignature);
    }
};

//------------------------------------------------------------------------------

/** Verify that a public key is authorized to sign on behalf of an account.
 *
 *  Handles three ledger-state cases:
 *  - **Unactivated account** (`accountState` is null): only the master key
 *    (whose derived `AccountID` matches `accountID`) is accepted.
 *  - **Active account, master enabled**: master key or regular key accepted.
 *  - **Active account, `lsfDisableMaster` set**: only the regular key accepted;
 *    presenting the master key returns `RpcMasterDisabled`.
 *
 *  @param accountState  Ledger entry for the account, or null if the account
 *      does not yet exist on the ledger.
 *  @param accountID     The `AccountID` that should authorize the transaction.
 *  @param publicKey     Public key supplied by the signer.
 *  @return `RpcSuccess` on match; `RpcBadSecret` or `RpcMasterDisabled` on failure.
 */
static ErrorCodeI
acctMatchesPubKey(
    std::shared_ptr<SLE const> accountState,
    AccountID const& accountID,
    PublicKey const& publicKey)
{
    auto const publicKeyAcctID = calcAccountID(publicKey);
    bool const isMasterKey = publicKeyAcctID == accountID;

    // Unactivated account: AccountID match alone is sufficient.
    if (!accountState)
    {
        if (isMasterKey)
            return RpcSuccess;
        return RpcBadSecret;
    }

    auto const& sle = *accountState;
    if (isMasterKey)
    {
        if (sle.isFlag(lsfDisableMaster))
            return RpcMasterDisabled;
        return RpcSuccess;
    }

    // Fall back to the account's designated regular key.
    if ((sle.isFieldPresent(sfRegularKey)) && (publicKeyAcctID == sle.getAccountID(sfRegularKey)))
    {
        return RpcSuccess;
    }
    return RpcBadSecret;
}

/** Validate and enrich a Payment transaction in `tx_json`.
 *
 *  Handles the `DeliverMax`/`Amount` alias, validates the destination address,
 *  and optionally runs the `Pathfinder` to auto-populate `Paths`.
 *
 *  Non-Payment transactions are accepted without modification (returns empty).
 *
 *  @param params        Full RPC request object; read for `build_path`,
 *      `fee_mult_max`, `fee_div_max`.
 *  @param txJson        The `tx_json` sub-object; modified in place when
 *      `DeliverMax` is normalized to `Amount` or when `Paths` is populated.
 *  @param srcAddressID  Sending account; used as the issuer for a default
 *      `SendMax` when `build_path` is requested.
 *  @param role          Request role; unlimited roles bypass the path-find
 *      concurrency gate.
 *  @param app           Application reference for ledger and config access.
 *  @param doPath        When true, path-finding and field auto-fill are
 *      permitted.  False in multi-signing and offline modes.
 *  @return Empty `json::Value` on success; an error object on failure.
 *  @note MPT-denominated amounts cannot use path-finding unless the
 *      `featureMPTokensV2` amendment is enabled.
 */
static json::Value
checkPayment(
    json::Value const& params,
    json::Value& txJson,
    AccountID const& srcAddressID,
    Role const role,
    Application& app,
    bool doPath)
{
    // Only path find for Payments.
    if (txJson[jss::TransactionType].asString() != jss::Payment)
        return json::Value();

    // DeliverMax is an alias to Amount and we use Amount internally
    if (txJson.isMember(jss::DeliverMax))
    {
        if (txJson.isMember(jss::Amount))
        {
            if (txJson[jss::DeliverMax] != txJson[jss::Amount])
            {
                return RPC::makeError(
                    RpcInvalidParams, "Cannot specify differing 'Amount' and 'DeliverMax'");
            }
        }
        else
        {
            txJson[jss::Amount] = txJson[jss::DeliverMax];
        }

        txJson.removeMember(jss::DeliverMax);
    }

    if (!txJson.isMember(jss::Amount))
        return RPC::missingFieldError("tx_json.Amount");

    STAmount amount;

    if (!amountFromJsonNoThrow(amount, txJson[jss::Amount]))
        return RPC::invalidFieldError("tx_json.Amount");

    if (!txJson.isMember(jss::Destination))
        return RPC::missingFieldError("tx_json.Destination");

    auto const dstAccountID = parseBase58<AccountID>(txJson[jss::Destination].asString());
    if (!dstAccountID)
        return RPC::invalidFieldError("tx_json.Destination");

    if (params.isMember(jss::build_path) &&
        (!doPath ||
         (!app.getOpenLedger().current()->rules().enabled(featureMPTokensV2) &&
          amount.holds<MPTIssue>())))
    {
        return RPC::makeError(RpcInvalidParams, "Field 'build_path' not allowed in this context.");
    }

    if (txJson.isMember(jss::Paths) && params.isMember(jss::build_path))
    {
        return RPC::makeError(
            RpcInvalidParams, "Cannot specify both 'tx_json.Paths' and 'build_path'");
    }

    std::optional<uint256> domain;
    if (txJson.isMember(sfDomainID.jsonName))
    {
        uint256 num;
        if (!txJson[sfDomainID.jsonName].isString() ||
            !num.parseHex(txJson[sfDomainID.jsonName].asString()))
        {
            return RPC::makeError(RpcDomainMalformed, "Unable to parse 'DomainID'.");
        }

        domain = num;
    }

    if (!txJson.isMember(jss::Paths) && params.isMember(jss::build_path))
    {
        STAmount sendMax;

        if (txJson.isMember(jss::SendMax))
        {
            if (!amountFromJsonNoThrow(sendMax, txJson[jss::SendMax]))
                return RPC::invalidFieldError("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer if Issue.
            sendMax = amount;
            sendMax.asset().visit(
                [&](Issue const&) { sendMax.get<Issue>().account = srcAddressID; },
                [](MPTIssue const&) {});
        }

        if (sendMax.native() && amount.native())
            return RPC::makeError(RpcInvalidParams, "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind const lpf(isUnlimited(role), app);
            if (!lpf.isOk())
                return rpcError(RpcTooBusy);

            STPathSet result;

            if (auto ledger = app.getOpenLedger().current())
            {
                Pathfinder pf(
                    std::make_shared<AssetCache>(ledger, app.getJournal("AssetCache")),
                    srcAddressID,
                    *dstAccountID,
                    sendMax.asset(),
                    sendMax.getIssuer(),
                    amount,
                    std::nullopt,
                    domain,
                    app);
                if (pf.findPaths(app.config().PATH_SEARCH_OLD))
                {
                    // 4 is the maximum paths
                    pf.computePathRanks(4);
                    STPath fullLiquidityPath;
                    STPathSet const paths;
                    result = pf.getBestPaths(4, fullLiquidityPath, paths, sendMax.getIssuer());
                }
            }

            auto j = app.getJournal("RPCHandler");
            JLOG(j.debug()) << "transactionSign: build_path: "
                            << result.getJson(JsonOptions::Values::None);

            if (!result.empty())
                txJson[jss::Paths] = result.getJson(JsonOptions::Values::None);
        }
    }
    return json::Value();
}

//------------------------------------------------------------------------------

/** Validate the core fields of `tx_json` without modifying them.
 *
 *  Checks that `tx_json` is an object and that `TransactionType` and `Account`
 *  are present and well-formed.  When in online (verify) mode also enforces
 *  ledger freshness and cluster load limits.  `Sequence` is deliberately
 *  not checked here because its requirements are context-sensitive.
 *
 *  @param txJson              The `tx_json` sub-object from the RPC request.
 *  @param role                Request role; unlimited roles bypass load checks.
 *  @param verify              True when online checks (age, load) should run;
 *      false in offline mode.
 *  @param validatedLedgerAge  Age of the last validated ledger.
 *  @param config              Node configuration.
 *  @param feeTrack            Current load-fee state.
 *  @param apiVersion          Negotiated API version; v1 emits `rpcNO_CURRENT`,
 *      v2+ emits `rpcNOT_SYNCED` for a stale ledger.
 *  @return A pair whose first element is an error `json::Value` (empty on
 *      success) and whose second element is the parsed source `AccountID`
 *      (default-constructed on failure).
 */
static std::pair<json::Value, AccountID>
checkTxJsonFields(
    json::Value const& txJson,
    Role const role,
    bool const verify,
    std::chrono::seconds validatedLedgerAge,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    unsigned apiVersion)
{
    std::pair<json::Value, AccountID> ret;

    if (!txJson.isObject())
    {
        ret.first = RPC::objectFieldError(jss::tx_json);
        return ret;
    }

    if (!txJson.isMember(jss::TransactionType))
    {
        ret.first = RPC::missingFieldError("tx_json.TransactionType");
        return ret;
    }

    if (!txJson.isMember(jss::Account))
    {
        ret.first = RPC::makeError(RpcSrcActMissing, RPC::missingFieldMessage("tx_json.Account"));
        return ret;
    }

    auto const srcAddressID = parseBase58<AccountID>(txJson[jss::Account].asString());

    if (!srcAddressID)
    {
        ret.first = RPC::makeError(RpcSrcActMalformed, RPC::invalidFieldMessage("tx_json.Account"));
        return ret;
    }

    // Check for current ledger.
    if (verify && !config.standalone() && (validatedLedgerAge > Tuning::kMAX_VALIDATED_LEDGER_AGE))
    {
        if (apiVersion == 1)
        {
            ret.first = rpcError(RpcNoCurrent);
        }
        else
        {
            ret.first = rpcError(RpcNotSynced);
        }
        return ret;
    }

    // Check for load.
    if (feeTrack.isLoadedCluster() && !isUnlimited(role))
    {
        ret.first = rpcError(RpcTooBusy);
        return ret;
    }

    // It's all good.  Return the AccountID.
    ret.second = *srcAddressID;
    return ret;
}

static Expected<void, json::Value>
checkNetworkID(json::Value const& txJson, uint32_t appNetworkId)
{
    if (appNetworkId > 1024)
    {
        if (!txJson.isMember(jss::NetworkID))
        {
            return Unexpected(
                RPC::makeError(RpcInvalidParams, RPC::missingFieldMessage("tx_json.NetworkID")));
        }
        if (!txJson[jss::NetworkID].isIntegral() || txJson[jss::NetworkID].asUInt() != appNetworkId)
        {
            return Unexpected(
                RPC::makeError(RpcInvalidParams, RPC::invalidFieldMessage("tx_json.NetworkID")));
        }
    }
    return Expected<void, json::Value>();
}

//------------------------------------------------------------------------------

/** Move-only discriminated union returned by `transactionPreProcessImpl`.
 *
 *  Carries either a `json::Value` error (when `second` is null) or a
 *  successfully constructed `STTx` (when `first` is empty).  Callers test
 *  `!result.second` to distinguish the two states.
 *
 *  Both fields are `const` and copy is deleted, so the two states are
 *  mutually exclusive and cannot be accidentally discarded.  This predates
 *  `std::expected<T,E>` and serves the same purpose.
 */
struct TransactionPreProcessResult
{
    /** Error object; non-empty iff pre-processing failed. */
    json::Value const first;
    /** Successfully constructed transaction; non-null iff pre-processing succeeded. */
    std::shared_ptr<STTx> const second;

    TransactionPreProcessResult() = delete;
    TransactionPreProcessResult(TransactionPreProcessResult const&) = delete;
    TransactionPreProcessResult(TransactionPreProcessResult&& rhs) = default;

    TransactionPreProcessResult&
    operator=(TransactionPreProcessResult const&) = delete;
    TransactionPreProcessResult&
    operator=(TransactionPreProcessResult&&) = delete;

    /** Construct the error state. */
    TransactionPreProcessResult(json::Value&& json) : first(std::move(json)), second()
    {
    }

    /** Construct the success state. */
    explicit TransactionPreProcessResult(std::shared_ptr<STTx>&& st)
        : first(), second(std::move(st))
    {
    }
};

/** Core signing pipeline shared by `transactionSign`, `transactionSubmit`,
 *  and `transactionSignFor`.
 *
 *  Executes the following steps in order:
 *  1. **Key extraction** — resolves the key pair from `secret`, `seed`,
 *     `seed_hex`, or `passphrase` in `params`.
 *  2. **Signature-target resolution** — if `signature_target` is present,
 *     looks up the `SField` and its `SOTemplate`; rejects unknown targets.
 *  3. **Field validation** — `checkTxJsonFields()` gates on `TransactionType`,
 *     `Account`, ledger freshness, and cluster load.
 *  4. **Sequence auto-fill** — in online single-signing mode, fetches the
 *     next queuable sequence from `TxQ`; ticket-based transactions receive 0;
 *     multi-signing skips auto-fill entirely (`editFields()` returns false).
 *  5. **NetworkID auto-fill** — networks with ID > 1024 have their ID injected
 *     to prevent cross-network replay.
 *  6. **Fee check** — delegates to `checkFee()`.
 *  7. **Payment validation** — delegates to `checkPayment()`.
 *  8. **Signing-mode exclusivity** — rejects `TxnSignature` when
 *     multi-signing and `Signers` when single-signing.
 *  9. **Account–key binding** — `acctMatchesPubKey()` validates the key
 *     against the account's master or regular key; for delegated transactions,
 *     the check runs against the delegate's ledger entry instead.
 *  10. **STTx construction** — `STParsedJSONObject` serializes `tx_json`;
 *      `SigningPubKey` is set to empty bytes for multi-signing or to the
 *      actual public key for single-signing.
 *  11. **Signing** — multi-signing calls `buildMultiSigningData()` and
 *      stores the result in `signingArgs`; single-signing calls `stTx->sign()`.
 *
 *  @param params             Full RPC request (modified in place when fields
 *      are auto-filled).
 *  @param role               Request role; affects load-shed and path-find
 *      concurrency limits.
 *  @param signingArgs        Mode discriminator and signature accumulator;
 *      populated with the public key and multi-signature on success.
 *  @param validatedLedgerAge Age of the last validated ledger.
 *  @param app                Application reference.
 *  @return `TransactionPreProcessResult` carrying either an error JSON value
 *      or the signed `STTx`.
 */
static TransactionPreProcessResult
transactionPreProcessImpl(
    json::Value& params,
    Role role,
    SigningForParams& signingArgs,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    auto j = app.getJournal("RPCHandler");

    json::Value jvResult;
    std::optional<std::pair<PublicKey, SecretKey>> keyPair = keypairForSignature(params, jvResult);
    if (!keyPair || containsError(jvResult))
        return jvResult;

    PublicKey const& pk = keyPair->first;
    SecretKey const& sk = keyPair->second;

    bool const verify = !(params.isMember(jss::offline) && params[jss::offline].asBool());

    auto const signatureTarget =
        [&params]() -> std::optional<std::reference_wrapper<SField const>> {
        if (params.isMember(jss::signature_target))
            return SField::getField(params[jss::signature_target].asString());
        return std::nullopt;
    }();

    // Make sure the signature target field is valid, if specified, and save the
    // template for use later
    auto const signatureTemplate = signatureTarget
        ? InnerObjectFormats::getInstance().findSOTemplateBySField(*signatureTarget)
        : nullptr;
    if (signatureTarget)
    {
        if (signatureTemplate == nullptr)
        {  // Invalid target field
            return RPC::makeError(RpcInvalidParams, signatureTarget->get().getName());
        }
        signingArgs.setSignatureTarget(signatureTarget);
    }

    if (!params.isMember(jss::tx_json))
        return RPC::missingFieldError(jss::tx_json);

    json::Value& txJson(params[jss::tx_json]);

    auto [txJsonResult, srcAddressID] = checkTxJsonFields(
        txJson,
        role,
        verify,
        validatedLedgerAge,
        app.config(),
        app.getFeeTrack(),
        getAPIVersionNumber(params, app.config().BETA_RPC_API));

    if (RPC::containsError(txJsonResult))
        return std::move(txJsonResult);

    // Offline mode: caller must supply Sequence because we cannot look it up.
    if (!verify && !txJson.isMember(jss::Sequence))
        return RPC::missingFieldError("tx_json.Sequence");

    std::shared_ptr<SLE const> sle;
    if (verify)
        sle = app.getOpenLedger().current()->read(keylet::account(srcAddressID));

    if (verify && !sle)
    {
        JLOG(j.debug()) << "transactionSign: Failed to find source account "
                        << "in current ledger: " << toBase58(srcAddressID);

        return rpcError(RpcSrcActNotFound);
    }

    if (signingArgs.editFields())
    {
        if (!txJson.isMember(jss::Sequence))
        {
            bool const hasTicketSeq = txJson.isMember(sfTicketSequence.jsonName);
            if (!hasTicketSeq && !sle)
            {
                JLOG(j.debug()) << "transactionSign: Failed to find source account "
                                << "in current ledger: " << toBase58(srcAddressID);

                return rpcError(RpcSrcActNotFound);
            }
            txJson[jss::Sequence] = hasTicketSeq ? 0 : app.getTxQ().nextQueuableSeq(sle).value();
        }

        if (!txJson.isMember(jss::NetworkID))
        {
            auto const networkId = app.getNetworkIDService().getNetworkID();
            if (networkId > 1024)
                txJson[jss::NetworkID] = to_string(networkId);
        }
    }

    {
        json::Value err = checkFee(
            params,
            role,
            verify && signingArgs.editFields(),
            app.config(),
            app.getFeeTrack(),
            app.getTxQ(),
            app);

        if (RPC::containsError(err))
            return err;
    }

    {
        json::Value err = checkPayment(
            params, txJson, srcAddressID, role, app, verify && signingArgs.editFields());

        if (RPC::containsError(err))
            return err;
    }

    if (signingArgs.isMultiSigning())
    {
        if (txJson.isMember(jss::TxnSignature))
            return rpcError(RpcAlreadySingleSig);

        signingArgs.setPublicKey(pk);
    }
    else if (signingArgs.isSingleSigning())
    {
        if (txJson.isMember(jss::Signers))
            return rpcError(RpcAlreadyMultisig);
    }

    if (verify)
    {
        JLOG(j.trace()) << "verify: " << toBase58(calcAccountID(pk)) << " : "
                        << toBase58(srcAddressID);

        // Skip account–key binding for multi-signing and alternate signature
        // targets: in those cases the signing account and tx Account need not
        // be the same identity.
        if (!signingArgs.isMultiSigning() && !signatureTarget)
        {
            if (txJson.isMember(sfDelegate.jsonName))
            {
                // Delegated transaction: check the key against the delegate's
                // ledger entry, not the transaction's Account field.
                auto const delegateJson = txJson[sfDelegate.jsonName];
                auto const ptrDelegatedAddressID = delegateJson.isString()
                    ? parseBase58<AccountID>(delegateJson.asString())
                    : std::nullopt;

                if (!ptrDelegatedAddressID)
                {
                    return RPC::makeError(
                        RpcSrcActMalformed, RPC::invalidFieldMessage("tx_json.Delegate"));
                }

                auto delegatedAddressID = *ptrDelegatedAddressID;
                auto delegatedSle =
                    app.getOpenLedger().current()->read(keylet::account(delegatedAddressID));
                if (!delegatedSle)
                    return rpcError(RpcDelegateActNotFound);

                auto const err = acctMatchesPubKey(delegatedSle, delegatedAddressID, pk);

                if (err != RpcSuccess)
                    return rpcError(err);
            }
            else
            {
                auto const err = acctMatchesPubKey(sle, srcAddressID, pk);

                if (err != RpcSuccess)
                    return rpcError(err);
            }
        }
    }

    STParsedJSONObject parsed(std::string(jss::tx_json), txJson);
    if (!parsed.object.has_value())
    {
        json::Value err;
        err[jss::error] = parsed.error[jss::error];
        err[jss::error_code] = parsed.error[jss::error_code];
        err[jss::error_message] = parsed.error[jss::error_message];
        return err;
    }

    std::shared_ptr<STTx> stTx;
    try
    {
        // Protocol requirement: SigningPubKey must be empty bytes for
        // multi-signing, or the actual public key for single-signing.
        STObject* sigObject = &*parsed.object;
        if (signatureTarget)
        {
            // If the target object doesn't exist, make one.
            if (!parsed.object->isFieldPresent(*signatureTarget))
            {
                parsed.object->setFieldObject(
                    *signatureTarget, STObject{*signatureTemplate, *signatureTarget});
            }
            sigObject = &parsed.object->peekFieldObject(*signatureTarget);
        }
        sigObject->setFieldVL(
            sfSigningPubKey, signingArgs.isMultiSigning() ? Slice(nullptr, 0) : pk.slice());

        stTx = std::make_shared<STTx>(std::move(parsed.object.value()));
    }
    catch (STObject::FieldErr const& err)
    {
        return RPC::makeError(RpcInvalidParams, err.what());
    }
    catch (std::exception&)
    {
        return RPC::makeError(
            RpcInternal, "Exception occurred constructing serialized transaction");
    }

    std::string reason;
    if (!passesLocalChecks(*stTx, reason))
        return RPC::makeError(RpcInvalidParams, reason);

    if (signingArgs.isMultiSigning())
    {
        Serializer const s = buildMultiSigningData(*stTx, signingArgs.getSigner());

        auto multisig = xrpl::sign(pk, sk, s.slice());

        signingArgs.moveMultiSignature(std::move(multisig));
    }
    else if (signingArgs.isSingleSigning())
    {
        stTx->sign(pk, sk, signatureTarget);
    }

    return TransactionPreProcessResult{std::move(stTx)};
}

/** Wrap a signed `STTx` in a `Transaction` and perform a sterilization check.
 *
 *  Serializes the transaction to bytes, deserializes into a fresh `STTx`,
 *  and confirms that the round-trip produces an equivalent object.  This
 *  defensive invariant guarantees that what is broadcast to the P2P network
 *  is byte-for-byte identical to what was signed, ruling out any internal
 *  representation bug.
 *
 *  When `app.checkSigs()` is false (configurable for testing/trusted
 *  environments), the hash router is pre-seeded with `Validity::SigGoodOnly`
 *  so the cryptographic signature check is skipped while structural
 *  correctness is still confirmed.
 *
 *  @param stTx   The signed transaction to wrap and verify.
 *  @param rules  Current ledger rules used for signature validation.
 *  @param app    Application reference.
 *  @return A pair whose first element is an error `json::Value` (empty on
 *      success) and whose second element is the constructed `Transaction`
 *      pointer (null on failure).
 */
static std::pair<json::Value, Transaction::pointer>
transactionConstructImpl(
    std::shared_ptr<STTx const> const& stTx,
    Rules const& rules,
    Application& app)
{
    std::pair<json::Value, Transaction::pointer> ret;

    Transaction::pointer tpTrans;
    {
        std::string reason;
        tpTrans = std::make_shared<Transaction>(stTx, reason, app);
        if (tpTrans->getStatus() != TransStatus::NEW)
        {
            ret.first = RPC::makeError(RpcInternal, "Unable to construct transaction: " + reason);
            return ret;
        }
    }
    try
    {
        // Sterilization: serialize → deserialize → equivalence check.
        // Catches any internal representation discrepancy before broadcast.
        {
            Serializer s;
            tpTrans->getSTransaction()->add(s);
            Blob const transBlob = s.getData();
            SerialIter sit{makeSlice(transBlob)};

            auto sttxNew = std::make_shared<STTx const>(sit);
            if (!app.checkSigs())
            {
                forceValidity(
                    app.getHashRouter(), sttxNew->getTransactionID(), Validity::SigGoodOnly);
            }
            if (checkValidity(app.getHashRouter(), *sttxNew, rules).first != Validity::Valid)
            {
                ret.first = RPC::makeError(RpcInternal, "Invalid signature.");
                return ret;
            }

            std::string reason;
            auto tpTransNew = std::make_shared<Transaction>(sttxNew, reason, app);

            if (tpTransNew)
            {
                if (!tpTransNew->getSTransaction()->isEquivalent(*tpTrans->getSTransaction()))
                {
                    tpTransNew.reset();
                }
                tpTrans = std::move(tpTransNew);
            }
        }
    }
    catch (std::exception&)
    {
        // Any exception here is treated as a sterilization failure.
        tpTrans.reset();
    }

    if (!tpTrans)
    {
        ret.first = RPC::makeError(RpcInternal, "Unable to sterilize transaction.");
        return ret;
    }
    ret.second = std::move(tpTrans);
    return ret;
}

/** Serialize a `Transaction` into the JSON response object.
 *
 *  Populates `tx_json`, `tx_blob`, and (when the result is known)
 *  `engine_result`, `engine_result_code`, and `engine_result_message`.
 *
 *  API version differences:
 *  - v2+: `tx_json` uses `DisableApiPriorV2` options and `hash` is promoted
 *    to a top-level field; `DeliverMax` is re-inserted via `insertDeliverMax`.
 *  - v1: `tx_json` uses default options; `hash` is embedded inside `tx_json`.
 *
 *  @param tpTrans    The constructed and signed transaction.
 *  @param apiVersion Negotiated API version.
 *  @return A `json::Value` object with the formatted response fields, or an
 *      error object if JSON serialization throws.
 */
static json::Value
transactionFormatResultImpl(Transaction::pointer tpTrans, unsigned apiVersion)
{
    json::Value jvResult;
    try
    {
        if (apiVersion > 1)
        {
            jvResult[jss::tx_json] = tpTrans->getJson(JsonOptions::Values::DisableApiPriorV2);
            jvResult[jss::hash] = to_string(tpTrans->getID());
        }
        else
        {
            jvResult[jss::tx_json] = tpTrans->getJson(JsonOptions::Values::None);
        }

        RPC::insertDeliverMax(
            jvResult[jss::tx_json], tpTrans->getSTransaction()->getTxnType(), apiVersion);

        jvResult[jss::tx_blob] = strHex(tpTrans->getSTransaction()->getSerializer().peekData());

        if (temUNCERTAIN != tpTrans->getResult())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo(tpTrans->getResult(), sToken, sHuman);

            jvResult[jss::engine_result] = sToken;
            jvResult[jss::engine_result_code] = tpTrans->getResult();
            jvResult[jss::engine_result_message] = sHuman;
        }
    }
    catch (std::exception&)
    {
        jvResult = RPC::makeError(RpcInternal, "Exception occurred during JSON handling.");
    }
    return jvResult;
}

}  // namespace detail

//------------------------------------------------------------------------------

/** Compute the base fee for a transaction by temporarily patching placeholder
 *  fields and calling `calculateBaseFee`.
 *
 *  The protocol fee depends on transaction type and content (e.g., number of
 *  signers), not type alone, so the transaction must be serialized to an
 *  `STTx` to call `calculateBaseFee()`.  Placeholder values are inserted into
 *  a local copy of `tx` for `Fee`, `Sequence`, `SigningPubKey`, and
 *  `TxnSignature` (and per-signer fields for multi-signed transactions) so
 *  that `STParsedJSONObject` can parse correctly.  The caller's `tx` is not
 *  modified.
 *
 *  Falls back to `config.FEES.reference_fee` if parsing fails or the
 *  transaction is structurally invalid.
 *
 *  @param app     Application reference for open ledger access.
 *  @param config  Node configuration for the fallback reference fee.
 *  @param tx      A copy (by value) of the `tx_json` object.
 *  @return The computed base fee in drops, or the reference fee on error.
 */
[[nodiscard]] static XRPAmount
getTxFee(Application const& app, Config const& config, json::Value tx)
{
    auto const& ledger = app.getOpenLedger().current();
    if (!tx.isMember(jss::Fee))
    {
        tx[jss::Fee] = "0";
    }

    if (!tx.isMember(jss::Sequence))
    {
        tx[jss::Sequence] = "0";
    }

    if (!tx.isMember(jss::SigningPubKey))
    {
        tx[jss::SigningPubKey] = "";
    }

    if (!tx.isMember(jss::TxnSignature))
    {
        tx[jss::TxnSignature] = "";
    }

    if (tx.isMember(jss::Signers))
    {
        if (!tx[jss::Signers].isArray())
            return config.FEES.reference_fee;

        if (tx[jss::Signers].size() > STTx::kMAX_MULTI_SIGNERS)
            return config.FEES.reference_fee;

        for (auto& signer : tx[jss::Signers])
        {
            if (!signer.isMember(jss::Signer) || !signer[jss::Signer].isObject())
                return config.FEES.reference_fee;
            if (!signer[jss::Signer].isMember(jss::SigningPubKey))
                signer[jss::Signer][jss::SigningPubKey] = "";
            if (!signer[jss::Signer].isMember(jss::TxnSignature))
                signer[jss::Signer][jss::TxnSignature] = "";
        }
    }

    STParsedJSONObject parsed(std::string(jss::tx_json), tx);
    if (!parsed.object.has_value())
    {
        return config.FEES.reference_fee;
    }

    try
    {
        STTx const& stTx = STTx(std::move(parsed.object.value()));
        std::string reason;
        if (!passesLocalChecks(stTx, reason))
            return config.FEES.reference_fee;

        return calculateBaseFee(*app.getOpenLedger().current(), stTx);
    }
    catch (std::exception& e)
    {
        return config.FEES.reference_fee;
    }
}

/** Compute the current recommended fee for a transaction, subject to a
 *  caller-supplied ceiling expressed as `feeDefault * mult / div`.
 *
 *  Pipeline:
 *  1. `getTxFee()` determines the protocol base fee.
 *  2. `scaleFeeLoad()` applies load scaling; admin/unlimited roles are exempt.
 *  3. The TxQ's current open-ledger escalated fee level is compared and the
 *     maximum of the two is taken, so the result is always sufficient to enter
 *     the open ledger.
 *  4. The ceiling `feeDefault * mult / div` is enforced; if the computed fee
 *     exceeds it, `rpcHIGH_FEE` is returned instead of a drops value.
 *
 *  @param role      Request role; unlimited roles bypass load scaling.
 *  @param config    Node configuration for reference fee fallback.
 *  @param feeTrack  Current node load-fee state.
 *  @param txQ       Transaction queue for escalated fee metrics.
 *  @param app       Application reference.
 *  @param tx        The `tx_json` object used to determine the base fee.
 *  @param mult      Ceiling multiplier (defaults to
 *      `Tuning::kDEFAULT_AUTO_FILL_FEE_MULTIPLIER`).
 *  @param div       Ceiling divisor (defaults to
 *      `Tuning::kDEFAULT_AUTO_FILL_FEE_DIVISOR`).
 *  @return A `json::Value` containing the fee in drops (as a clipped integer),
 *      or an error object with `rpcHIGH_FEE` if the ceiling is breached.
 *  @throws std::overflow_error if `mulDiv(feeDefault, mult, div)` overflows.
 */
json::Value
getCurrentNetworkFee(
    Role const role,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app,
    json::Value const& tx,
    int mult,
    int div)
{
    XRPAmount const feeDefault = getTxFee(app, config, tx);

    auto ledger = app.getOpenLedger().current();
    // Admin/unlimited endpoints are exempt from local load fee scaling.
    XRPAmount const loadFee = scaleFeeLoad(feeDefault, feeTrack, ledger->fees(), isUnlimited(role));
    XRPAmount fee = loadFee;
    {
        auto const metrics = txQ.getMetrics(*ledger);
        auto const baseFee = ledger->fees().base;
        auto escalatedFee = toDrops(metrics.openLedgerFeeLevel - FeeLevel64(1), baseFee) + 1;
        fee = std::max(fee, escalatedFee);
    }

    auto const limit = mulDiv(feeDefault, mult, div);
    if (!limit)
        Throw<std::overflow_error>("mulDiv");

    if (fee > *limit)
    {
        std::stringstream ss;
        ss << "Fee of " << fee << " exceeds the requested tx limit of " << *limit;
        return RPC::makeError(RpcHighFee, ss.str());
    }

    return fee.jsonClipped();
}

json::Value
checkFee(
    json::Value& request,
    Role const role,
    bool doAutoFill,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app)
{
    json::Value& tx(request[jss::tx_json]);
    if (tx.isMember(jss::Fee))
        return json::Value();

    if (!doAutoFill)
        return RPC::missingFieldError("tx_json.Fee");

    int mult = Tuning::kDEFAULT_AUTO_FILL_FEE_MULTIPLIER;
    int div = Tuning::kDEFAULT_AUTO_FILL_FEE_DIVISOR;
    if (request.isMember(jss::fee_mult_max))
    {
        if (request[jss::fee_mult_max].isInt())
        {
            mult = request[jss::fee_mult_max].asInt();
            if (mult < 0)
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(jss::fee_mult_max, "a positive integer"));
            }
        }
        else
        {
            return RPC::makeError(
                RpcHighFee, RPC::expectedFieldMessage(jss::fee_mult_max, "a positive integer"));
        }
    }
    if (request.isMember(jss::fee_div_max))
    {
        if (request[jss::fee_div_max].isInt())
        {
            div = request[jss::fee_div_max].asInt();
            if (div <= 0)
            {
                return RPC::makeError(
                    RpcInvalidParams,
                    RPC::expectedFieldMessage(jss::fee_div_max, "a positive integer"));
            }
        }
        else
        {
            return RPC::makeError(
                RpcHighFee, RPC::expectedFieldMessage(jss::fee_div_max, "a positive integer"));
        }
    }

    auto feeOrError = getCurrentNetworkFee(role, config, feeTrack, txQ, app, tx, mult, div);
    if (feeOrError.isMember(jss::error))
        return feeOrError;
    tx[jss::Fee] = std::move(feeOrError);
    return json::Value();
}

//------------------------------------------------------------------------------

/** Validate, sign, and return a transaction without submitting it to the
 *  network (implements the `sign` RPC command).
 *
 *  Runs the full pre-processing pipeline in single-signing mode, sterilizes
 *  the result, and formats the response.  The transaction is not forwarded
 *  to `NetworkOPs`; the caller receives `tx_blob` and `tx_json` to submit
 *  independently.
 *
 *  @param jvRequest          Full RPC request JSON (passed by value; modified
 *      in place during field auto-fill).
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Failure mode for `processTransaction` (unused
 *      here but kept for API symmetry with `transactionSubmit`).
 *  @param role               Request role.
 *  @param validatedLedgerAge Age of the last validated ledger.
 *  @param app                Application reference.
 *  @return JSON object with `tx_json`, `tx_blob`, and optional engine result
 *      fields; or an error object on failure.
 */
json::Value
transactionSign(
    json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    using namespace detail;

    auto j = app.getJournal("RPCHandler");
    JLOG(j.debug()) << "transactionSign: " << jvRequest;

    SigningForParams signForParams;
    TransactionPreProcessResult const preprocResult =
        transactionPreProcessImpl(jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    std::shared_ptr<ReadView const> const ledger = app.getOpenLedger().current();
    std::pair<json::Value, Transaction::pointer> const txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Validate, sign, and immediately submit a transaction to the network
 *  (implements the `submit` RPC command when no `tx_blob` is provided).
 *
 *  Identical to `transactionSign` but additionally calls `processTransaction`
 *  to forward the transaction to `NetworkOPs`.  The response includes the
 *  engine result from that submission.
 *
 *  @param jvRequest          Full RPC request JSON (passed by value; modified
 *      in place during field auto-fill).
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Whether to treat submission failure as hard.
 *  @param role               Request role.
 *  @param validatedLedgerAge Age of the last validated ledger.
 *  @param app                Application reference.
 *  @param processTransaction Dependency-injected submission function; use
 *      `getProcessTxnFn(NetworkOPs&)` in production.
 *  @return JSON object with `tx_json`, `tx_blob`, and engine result fields;
 *      or an error object on failure.
 *  @note The submission uses the synchronous `processTransaction` interface.
 *      An async path would improve throughput but is not yet implemented
 *      (see FIXME comment in source).
 */
json::Value
transactionSubmit(
    json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    using namespace detail;

    auto const& ledger = app.getOpenLedger().current();
    auto j = app.getJournal("RPCHandler");
    JLOG(j.debug()) << "transactionSubmit: " << jvRequest;

    SigningForParams signForParams;
    TransactionPreProcessResult const preprocResult =
        transactionPreProcessImpl(jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    std::pair<json::Value, Transaction::pointer> txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    try
    {
        // FIXME: For performance, should use async interface
        processTransaction(txn.second, isUnlimited(role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::makeError(RpcInternal, "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl(txn.second, apiVersion);
}

namespace detail {

/** Validate the multi-signing prerequisites common to `transactionSignFor`
 *  and `transactionSubmitMultiSigned`.
 *
 *  Checks that `tx_json` is present and is an object, that `Sequence` is
 *  present (callers must supply it; auto-fill is disabled for multi-signing),
 *  and that `SigningPubKey` is present and empty — unless a
 *  `signature_target` is specified, in which case the key routes into an
 *  inner object and need not be empty at the root.
 *
 *  These checks run before serialization so that error messages are
 *  field-specific rather than generic parse failures.
 *
 *  @param jvRequest  Full RPC request JSON.
 *  @return Empty `json::Value` on success; an error object on failure.
 */
static json::Value
checkMultiSignFields(json::Value const& jvRequest)
{
    if (!jvRequest.isMember(jss::tx_json))
        return RPC::missingFieldError(jss::tx_json);

    json::Value const& txJson(jvRequest[jss::tx_json]);

    if (!txJson.isObject())
        return RPC::invalidFieldMessage(jss::tx_json);

    if (!txJson.isMember(jss::Sequence))
        return RPC::missingFieldError("tx_json.Sequence");

    if (!txJson.isMember(sfSigningPubKey.getJsonName()))
        return RPC::missingFieldError("tx_json.SigningPubKey");

    // When using signature_target, the signature goes into an inner object
    // rather than the transaction root, so the root SigningPubKey need not
    // be empty.
    if (!jvRequest.isMember(jss::signature_target) &&
        !txJson[sfSigningPubKey.getJsonName()].asString().empty())
    {
        return RPC::makeError(
            RpcInvalidParams, "When multi-signing 'tx_json.SigningPubKey' must be empty.");
    }

    return json::Value();
}

/** Sort and validate the `sfSigners` array in preparation for submission.
 *
 *  The protocol requires the Signers array to be sorted in ascending order
 *  by `AccountID`.  This function sorts in place, then checks for duplicate
 *  accounts (disallowed) and self-signing (the transaction's own account
 *  may not appear as a signer).
 *
 *  @param signers       The `STArray` of `Signer` objects to sort and validate.
 *  @param signingForID  The `AccountID` of the transaction's `Account` field;
 *      used to reject self-signing.
 *  @return Empty `json::Value` on success; a param-error object on failure.
 */
static json::Value
sortAndValidateSigners(STArray& signers, AccountID const& signingForID)
{
    if (signers.empty())
        return RPC::makeParamError("Signers array may not be empty.");

    std::ranges::sort(signers, [](STObject const& a, STObject const& b) {
        return (a[sfAccount] < b[sfAccount]);
    });

    auto const dupIter = std::ranges::adjacent_find(
        signers,
        [](STObject const& a, STObject const& b) { return (a[sfAccount] == b[sfAccount]); });

    if (dupIter != signers.end())
    {
        std::ostringstream err;
        err << "Duplicate Signers:Signer:Account entries (" << toBase58((*dupIter)[sfAccount])
            << ") are not allowed.";
        return RPC::makeParamError(err.str());
    }

    if (signers.end() != std::ranges::find_if(signers, [&signingForID](STObject const& elem) {
            return elem[sfAccount] == signingForID;
        }))
    {
        std::ostringstream err;
        err << "A Signer may not be the transaction's Account (" << toBase58(signingForID) << ").";
        return RPC::makeParamError(err.str());
    }
    return {};
}

}  // namespace detail

/** Add one signer's contribution to an in-progress multi-signed transaction
 *  (implements the `sign_for` RPC command).
 *
 *  Parses the `account` field (the signing party), constructs `SigningForParams`
 *  in multi-signing mode, runs `transactionPreProcessImpl` to compute the
 *  cryptographic multi-signature, then injects a new `Signer` entry into the
 *  `sfSigners` array.  After injection, `sortAndValidateSigners` sorts the array
 *  by `AccountID` (a protocol requirement) and rejects duplicates or
 *  self-signing.
 *
 *  Does not submit the transaction; use `transactionSubmitMultiSigned` once
 *  all signers have contributed.
 *
 *  @param jvRequest          Full RPC request JSON (passed by value).
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Unused; present for API symmetry.
 *  @param role               Request role.
 *  @param validatedLedgerAge Age of the last validated ledger.
 *  @param app                Application reference.
 *  @return JSON object with `tx_json` (updated Signers array), `tx_blob`,
 *      and optional engine result fields; or an error object on failure.
 */
json::Value
transactionSignFor(
    json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    auto const& ledger = app.getOpenLedger().current();
    auto j = app.getJournal("RPCHandler");
    JLOG(j.debug()) << "transactionSignFor: " << jvRequest;

    char const accountField[] = "account";

    if (!jvRequest.isMember(accountField))
        return RPC::missingFieldError(accountField);

    auto const signerAccountID = parseBase58<AccountID>(jvRequest[accountField].asString());
    if (!signerAccountID)
    {
        return RPC::makeError(RpcSrcActMalformed, RPC::invalidFieldMessage(accountField));
    }

    if (!jvRequest.isMember(jss::tx_json))
        return RPC::missingFieldError(jss::tx_json);

    {
        json::Value& txJson(jvRequest[jss::tx_json]);

        if (!txJson.isObject())
            return RPC::objectFieldError(jss::tx_json);

        if (auto checkResult =
                detail::checkNetworkID(txJson, app.getNetworkIDService().getNetworkID());
            !checkResult)
        {
            return std::move(checkResult).error();
        }

        // Insert an empty SigningPubKey if absent — the multi-signing protocol
        // requires the root SigningPubKey to be empty bytes, and
        // `checkMultiSignFields` would otherwise reject non-multisign txns here.
        if (!txJson.isMember(sfSigningPubKey.getJsonName()))
            txJson[sfSigningPubKey.getJsonName()] = "";
    }

    // Sequence and SigningPubKey must be supplied by the caller; auto-fill
    // is disabled for multi-signing.
    using namespace detail;
    {
        json::Value err = checkMultiSignFields(jvRequest);
        if (RPC::containsError(err))
            return err;
    }

    SigningForParams signForParams(*signerAccountID);

    TransactionPreProcessResult const preprocResult =
        transactionPreProcessImpl(jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    XRPL_ASSERT(
        signForParams.validMultiSign(), "xrpl::RPC::transactionSignFor : valid multi-signature");

    {
        std::shared_ptr<SLE const> const accountState =
            ledger->read(keylet::account(*signerAccountID));
        auto const err =
            acctMatchesPubKey(accountState, *signerAccountID, signForParams.getPublicKey());

        if (err != RpcSuccess)
            return rpcError(err);
    }

    auto& sttx = preprocResult.second;
    {
        STObject signer = STObject::makeInnerObject(sfSigner);
        signer[sfAccount] = *signerAccountID;
        signer.setFieldVL(sfTxnSignature, signForParams.getSignature());
        signer.setFieldVL(sfSigningPubKey, signForParams.getPublicKey().slice());

        STObject& sigTarget = [&]() -> STObject& {
            auto const target = signForParams.getSignatureTarget();
            if (target)
                return sttx->peekFieldObject(*target);
            return *sttx;
        }();
        if (!sigTarget.isFieldPresent(sfSigners))
            sigTarget.setFieldArray(sfSigners, {});

        auto& signers = sigTarget.peekFieldArray(sfSigners);
        signers.emplaceBack(std::move(signer));

        auto err = sortAndValidateSigners(signers, (*sttx)[sfAccount]);
        if (RPC::containsError(err))
            return err;
    }

    std::pair<json::Value, Transaction::pointer> const txn =
        transactionConstructImpl(sttx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Validate and submit a fully multi-signed transaction to the network
 *  (implements the `submit_multisigned` RPC command).
 *
 *  Unlike `transactionSignFor`, this function does not re-sign.  It assumes
 *  all signers have already contributed via `transactionSignFor` and the
 *  `Signers` array is complete.  Validation steps in order:
 *  1. `checkMultiSignFields` — ensures `Sequence` and `SigningPubKey` are present.
 *  2. `checkTxJsonFields` — validates `TransactionType`, `Account`, ledger age.
 *  3. Confirms source account exists on the ledger.
 *  4. `checkFee` — Fee must be present and is not auto-filled here.
 *  5. `checkPayment` — validates Payment-specific fields (no path-find).
 *  6. Serializes `tx_json` to `STTx`.
 *  7. Structural checks: `SigningPubKey` must be empty, no `TxnSignature`,
 *     `Fee` must be XRP and > 0.
 *  8. `sortAndValidateSigners` — sorts and validates the `Signers` array.
 *  9. `transactionConstructImpl` — sterilization round-trip.
 *  10. Submits via `processTransaction`.
 *
 *  @param jvRequest          Full RPC request JSON (passed by value).
 *  @param apiVersion         Negotiated API version.
 *  @param failType           Whether to treat submission failure as hard.
 *  @param role               Request role.
 *  @param validatedLedgerAge Age of the last validated ledger.
 *  @param app                Application reference.
 *  @param processTransaction Dependency-injected submission function.
 *  @return JSON object with `tx_json`, `tx_blob`, and engine result fields;
 *      or an error object on failure.
 */
json::Value
transactionSubmitMultiSigned(
    json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    auto const& ledger = app.getOpenLedger().current();
    auto j = app.getJournal("RPCHandler");
    JLOG(j.debug()) << "transactionSubmitMultiSigned: " << jvRequest;

    // Sequence and SigningPubKey must be caller-supplied; no auto-fill for
    // multi-signing.
    using namespace detail;
    {
        json::Value err = checkMultiSignFields(jvRequest);
        if (RPC::containsError(err))
            return err;
    }

    json::Value& txJson(jvRequest["tx_json"]);

    auto [txJsonResult, srcAddressID] = checkTxJsonFields(
        txJson,
        role,
        true,
        validatedLedgerAge,
        app.config(),
        app.getFeeTrack(),
        getAPIVersionNumber(jvRequest, app.config().BETA_RPC_API));

    if (RPC::containsError(txJsonResult))
        return std::move(txJsonResult);

    std::shared_ptr<SLE const> const sle = ledger->read(keylet::account(srcAddressID));

    if (!sle)
    {
        JLOG(j.debug()) << "transactionSubmitMultiSigned: Failed to find source account "
                        << "in current ledger: " << toBase58(srcAddressID);

        return rpcError(RpcSrcActNotFound);
    }

    {
        json::Value err =
            checkFee(jvRequest, role, false, app.config(), app.getFeeTrack(), app.getTxQ(), app);

        if (RPC::containsError(err))
            return err;

        err = checkPayment(jvRequest, txJson, srcAddressID, role, app, false);

        if (RPC::containsError(err))
            return err;
    }

    std::shared_ptr<STTx> stTx;
    {
        STParsedJSONObject parsedTxJson("tx_json", txJson);
        if (!parsedTxJson.object)
        {
            json::Value jvResult;
            jvResult["error"] = parsedTxJson.error["error"];
            jvResult["error_code"] = parsedTxJson.error["error_code"];
            jvResult["error_message"] = parsedTxJson.error["error_message"];
            return jvResult;
        }
        try
        {
            stTx = std::make_shared<STTx>(std::move(parsedTxJson.object.value()));
        }
        catch (STObject::FieldErr const& err)
        {
            return RPC::makeError(RpcInvalidParams, err.what());
        }
        catch (std::exception& ex)
        {
            std::string const reason(ex.what());
            return RPC::makeError(
                RpcInternal, "Exception while serializing transaction: " + reason);
        }
        std::string reason;
        if (!passesLocalChecks(*stTx, reason))
            return RPC::makeError(RpcInvalidParams, reason);
    }

    // Structural validation: SigningPubKey empty, no TxnSignature, Fee in XRP and > 0.
    {
        if (!stTx->getFieldVL(sfSigningPubKey).empty())
        {
            std::ostringstream err;
            err << "Invalid  " << sfSigningPubKey.fieldName
                << " field.  Field must be empty when multi-signing.";
            return RPC::makeError(RpcInvalidParams, err.str());
        }

        if (stTx->isFieldPresent(sfTxnSignature))
            return rpcError(RpcSigningMalformed);

        auto const fee = stTx->getFieldAmount(sfFee);

        if (!isLegalNet(fee))
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName << " field.  Fees must be specified in XRP.";
            return RPC::makeError(RpcInvalidParams, err.str());
        }
        if (fee <= STAmount{0})
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName << " field.  Fees must be greater than zero.";
            return RPC::makeError(RpcInvalidParams, err.str());
        }
    }

    if (!stTx->isFieldPresent(sfSigners))
        return RPC::missingFieldError("tx_json.Signers");

    // SField guarantees sfSigners is an array when present.
    auto& signers = stTx->peekFieldArray(sfSigners);

    if (signers.empty())
        return RPC::makeParamError("tx_json.Signers array may not be empty.");

    // Each Signer entry must contain exactly Account, SigningPubKey, and
    // TxnSignature — no more, no fewer.
    if (std::ranges::find_if_not(signers, [](STObject const& obj) {
            return (
                obj.isFieldPresent(sfAccount) && obj.isFieldPresent(sfSigningPubKey) &&
                obj.isFieldPresent(sfTxnSignature) && obj.getCount() == 3);
        }) != signers.end())
    {
        return RPC::makeParamError("Signers array may only contain Signer entries.");
    }

    auto err = sortAndValidateSigners(signers, srcAddressID);
    if (RPC::containsError(err))
        return err;

    std::pair<json::Value, Transaction::pointer> txn =
        transactionConstructImpl(stTx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    try
    {
        // FIXME: For performance, should use async interface
        processTransaction(txn.second, isUnlimited(role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::makeError(RpcInternal, "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl(txn.second, apiVersion);
}

}  // namespace xrpl::RPC
