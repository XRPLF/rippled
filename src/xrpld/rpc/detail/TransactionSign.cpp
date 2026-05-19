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

// Used to pass extra parameters used when returning a
// a SigningFor object.
class SigningForParams
{
private:
    AccountID const* const multiSigningAcctID_;
    std::optional<PublicKey> multiSignPublicKey_;
    Buffer multiSignature_;
    std::optional<std::reference_wrapper<SField const>> signatureTarget_;

public:
    explicit SigningForParams() : multiSigningAcctID_(nullptr)
    {
    }

    SigningForParams(SigningForParams const& rhs) = delete;

    SigningForParams(AccountID const& multiSigningAcctID) : multiSigningAcctID_(&multiSigningAcctID)
    {
    }

    [[nodiscard]] bool
    isMultiSigning() const
    {
        return multiSigningAcctID_ != nullptr;
    }

    [[nodiscard]] bool
    isSingleSigning() const
    {
        return !isMultiSigning();
    }

    // When multi-signing we should not edit the tx_json fields.
    [[nodiscard]] bool
    editFields() const
    {
        return !isMultiSigning();
    }

    [[nodiscard]] bool
    validMultiSign() const
    {
        return isMultiSigning() && multiSignPublicKey_ && !multiSignature_.empty();
    }

    // Don't call this method unless isMultiSigning() returns true.
    [[nodiscard]] AccountID const&
    getSigner() const
    {
        if (multiSigningAcctID_ == nullptr)
            logicError("Accessing unknown SigningForParams::getSigner()");
        return *multiSigningAcctID_;
    }

    [[nodiscard]] PublicKey const&
    getPublicKey() const
    {
        if (!multiSignPublicKey_)
            logicError("Accessing unknown SigningForParams::getPublicKey()");
        return *multiSignPublicKey_;
    }

    [[nodiscard]] Buffer const&
    getSignature() const
    {
        return multiSignature_;
    }

    [[nodiscard]] std::optional<std::reference_wrapper<SField const>> const&
    getSignatureTarget() const
    {
        return signatureTarget_;
    }

    void
    setPublicKey(PublicKey const& multiSignPublicKey)
    {
        multiSignPublicKey_ = multiSignPublicKey;
    }

    void
    setSignatureTarget(std::optional<std::reference_wrapper<SField const>> const& field)
    {
        signatureTarget_ = field;
    }

    void
    moveMultiSignature(Buffer&& multiSignature)
    {
        multiSignature_ = std::move(multiSignature);
    }
};

//------------------------------------------------------------------------------

static ErrorCodeI
acctMatchesPubKey(
    std::shared_ptr<SLE const> accountState,
    AccountID const& accountID,
    PublicKey const& publicKey)
{
    auto const publicKeyAcctID = calcAccountID(publicKey);
    bool const isMasterKey = publicKeyAcctID == accountID;

    // If we can't get the accountRoot, but the accountIDs match, that's
    // good enough.
    if (!accountState)
    {
        if (isMasterKey)
            return RpcSuccess;
        return RpcBadSecret;
    }

    // If we *can* get to the accountRoot, check for MASTER_DISABLED.
    auto const& sle = *accountState;
    if (isMasterKey)
    {
        if (sle.isFlag(lsfDisableMaster))
            return RpcMasterDisabled;
        return RpcSuccess;
    }

    // The last gasp is that we have public Regular key.
    if ((sle.isFieldPresent(sfRegularKey)) && (publicKeyAcctID == sle.getAccountID(sfRegularKey)))
    {
        return RpcSuccess;
    }
    return RpcBadSecret;
}

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

// Validate (but don't modify) the contents of the tx_json.
//
// Returns a pair<json::Value, AccountID>.  The json::Value will contain error
// information if there was an error. On success, the account ID is returned
// and the json::Value will be empty.
//
// This code does not check the "Sequence" field, since the expectations
// for that field are particularly context sensitive.
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
    if (verify && !config.standalone() && (validatedLedgerAge > Tuning::kMaxValidatedLedgerAge))
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

// A move-only struct that makes it easy to return either a json::Value or a
// std::shared_ptr<STTx const> from transactionPreProcessImpl ().
struct TransactionPreProcessResult
{
    json::Value const first;
    std::shared_ptr<STTx> const second;

    TransactionPreProcessResult() = delete;
    TransactionPreProcessResult(TransactionPreProcessResult const&) = delete;
    TransactionPreProcessResult(TransactionPreProcessResult&& rhs) = default;

    TransactionPreProcessResult&
    operator=(TransactionPreProcessResult const&) = delete;
    TransactionPreProcessResult&
    operator=(TransactionPreProcessResult&&) = delete;

    TransactionPreProcessResult(json::Value&& json) : first(std::move(json)), second()
    {
    }

    explicit TransactionPreProcessResult(std::shared_ptr<STTx>&& st)
        : first(), second(std::move(st))
    {
    }
};

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

    // Check tx_json fields, but don't add any.
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

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !txJson.isMember(jss::Sequence))
        return RPC::missingFieldError("tx_json.Sequence");

    std::shared_ptr<SLE const> sle;
    if (verify)
        sle = app.getOpenLedger().current()->read(keylet::account(srcAddressID));

    if (verify && !sle)
    {
        // If not offline and did not find account, error.
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

    // If multisigning there should not be a single signature and vice versa.
    if (signingArgs.isMultiSigning())
    {
        if (txJson.isMember(jss::TxnSignature))
            return rpcError(RpcAlreadySingleSig);

        // If multisigning then we need to return the public key.
        signingArgs.setPublicKey(pk);
    }
    else if (signingArgs.isSingleSigning())
    {
        if (txJson.isMember(jss::Signers))
            return rpcError(RpcAlreadyMultisig);
    }

    if (verify)
    {
        // sle validity is checked above
        JLOG(j.trace()) << "verify: " << toBase58(calcAccountID(pk)) << " : "
                        << toBase58(srcAddressID);

        // Don't do this test if multisigning or if the signature is going into
        // an alternate field since the account and secret probably don't belong
        // together in that case.
        if (!signingArgs.isMultiSigning() && !signatureTarget)
        {
            // Make sure the account and secret belong together.
            if (txJson.isMember(sfDelegate.jsonName))
            {
                // Delegated transaction
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
        // If we're generating a multi-signature the SigningPubKey must be
        // empty, otherwise it must be the master account's public key.
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

    // If multisign then return multiSignature, else set TxnSignature field.
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

static std::pair<json::Value, Transaction::pointer>
transactionConstructImpl(
    std::shared_ptr<STTx const> const& stTx,
    Rules const& rules,
    Application& app)
{
    std::pair<json::Value, Transaction::pointer> ret;

    // Turn the passed in STTx into a Transaction.
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
        // Make sure the Transaction we just built is legit by serializing it
        // and then de-serializing it.  If the result isn't equivalent
        // to the initial transaction then there's something wrong with the
        // passed-in STTx.
        {
            Serializer s;
            tpTrans->getSTransaction()->add(s);
            Blob const transBlob = s.getData();
            SerialIter sit{makeSlice(transBlob)};

            // Check the signature if that's called for.
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
        // Assume that any exceptions are related to transaction sterilization.
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

[[nodiscard]] static XRPAmount
getTxFee(Application const& app, Config const& config, json::Value tx)
{
    auto const& ledger = app.getOpenLedger().current();
    // autofilling only needed in this function so that the `STParsedJSONObject`
    // parsing works properly it should not be modifying the actual `tx` object
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

        if (tx[jss::Signers].size() > STTx::kMaxMultiSigners)
            return config.FEES.reference_fee;

        // check multi-signed signers
        for (auto& signer : tx[jss::Signers])
        {
            if (!signer.isMember(jss::Signer) || !signer[jss::Signer].isObject())
                return config.FEES.reference_fee;
            if (!signer[jss::Signer].isMember(jss::SigningPubKey))
            {
                // autofill SigningPubKey
                signer[jss::Signer][jss::SigningPubKey] = "";
            }
            if (!signer[jss::Signer].isMember(jss::TxnSignature))
            {
                // autofill TxnSignature
                signer[jss::Signer][jss::TxnSignature] = "";
            }
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
    // Administrative and identified endpoints are exempt from local fees.
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

    int mult = Tuning::kDefaultAutoFillFeeMultiplier;
    int div = Tuning::kDefaultAutoFillFeeDivisor;
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

/** Returns a json::ValueType::Object. */
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

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    TransactionPreProcessResult const preprocResult =
        transactionPreProcessImpl(jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    std::shared_ptr<ReadView const> const ledger = app.getOpenLedger().current();
    // Make sure the STTx makes a legitimate Transaction.
    std::pair<json::Value, Transaction::pointer> const txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Returns a json::ValueType::Object. */
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

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    TransactionPreProcessResult const preprocResult =
        transactionPreProcessImpl(jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair<json::Value, Transaction::pointer> txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
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
// There are a some field checks shared by transactionSignFor
// and transactionSubmitMultiSigned.  Gather them together here.
static json::Value
checkMultiSignFields(json::Value const& jvRequest)
{
    if (!jvRequest.isMember(jss::tx_json))
        return RPC::missingFieldError(jss::tx_json);

    json::Value const& txJson(jvRequest[jss::tx_json]);

    if (!txJson.isObject())
        return RPC::invalidFieldMessage(jss::tx_json);

    // There are a couple of additional fields we need to check before
    // we serialize.  If we serialize first then we generate less useful
    // error messages.
    if (!txJson.isMember(jss::Sequence))
        return RPC::missingFieldError("tx_json.Sequence");

    if (!txJson.isMember(sfSigningPubKey.getJsonName()))
        return RPC::missingFieldError("tx_json.SigningPubKey");

    // Multi-signing into a signature_target object field is fine,
    // because it means the signature is not for the transaction
    // Account.
    if (!jvRequest.isMember(jss::signature_target) &&
        !txJson[sfSigningPubKey.getJsonName()].asString().empty())
    {
        return RPC::makeError(
            RpcInvalidParams, "When multi-signing 'tx_json.SigningPubKey' must be empty.");
    }

    return json::Value();
}

// Sort and validate an stSigners array.
//
// Returns a null json::Value if there are no errors.
static json::Value
sortAndValidateSigners(STArray& signers, AccountID const& signingForID)
{
    if (signers.empty())
        return RPC::makeParamError("Signers array may not be empty.");

    // Signers must be sorted by Account.
    std::ranges::sort(signers, [](STObject const& a, STObject const& b) {
        return (a[sfAccount] < b[sfAccount]);
    });

    // Signers may not contain any duplicates.
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

    // An account may not sign for itself.
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

/** Returns a json::ValueType::Object. */
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

    // Verify presence of the signer's account field.
    char const accountField[] = "account";

    if (!jvRequest.isMember(accountField))
        return RPC::missingFieldError(accountField);

    // Turn the signer's account into an AccountID for multi-sign.
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

        // If the tx_json.SigningPubKey field is missing, insert an empty one,
        // in order for the `checkMultiSignFields` to not return an error
        // for non-multisign transactions.
        if (!txJson.isMember(sfSigningPubKey.getJsonName()))
            txJson[sfSigningPubKey.getJsonName()] = "";
    }

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        json::Value err = checkMultiSignFields(jvRequest);
        if (RPC::containsError(err))
            return err;
    }

    // Add and amend fields based on the transaction type.
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
        // Make sure the account and secret belong together.
        auto const err =
            acctMatchesPubKey(accountState, *signerAccountID, signForParams.getPublicKey());

        if (err != RpcSuccess)
            return rpcError(err);
    }

    // Inject the newly generated signature into tx_json.Signers.
    auto& sttx = preprocResult.second;
    {
        // Make the signer object that we'll inject.
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
        // If there is not yet a Signers array, make one.
        if (!sigTarget.isFieldPresent(sfSigners))
            sigTarget.setFieldArray(sfSigners, {});

        auto& signers = sigTarget.peekFieldArray(sfSigners);
        signers.emplaceBack(std::move(signer));

        // The array must be sorted and validated.
        auto err = sortAndValidateSigners(signers, (*sttx)[sfAccount]);
        if (RPC::containsError(err))
            return err;
    }

    // Make sure the STTx makes a legitimate Transaction.
    std::pair<json::Value, Transaction::pointer> const txn =
        transactionConstructImpl(sttx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Returns a json::ValueType::Object. */
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

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
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
        // If did not find account, error.
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

    // Grind through the JSON in tx_json to produce a STTx.
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

    // Validate the fields in the serialized transaction.
    {
        // We now have the transaction text serialized and in the right format.
        // Verify the values of select fields.
        //
        // The SigningPubKey must be present but empty.
        if (!stTx->getFieldVL(sfSigningPubKey).empty())
        {
            std::ostringstream err;
            err << "Invalid  " << sfSigningPubKey.fieldName
                << " field.  Field must be empty when multi-signing.";
            return RPC::makeError(RpcInvalidParams, err.str());
        }

        // There may not be a TxnSignature field.
        if (stTx->isFieldPresent(sfTxnSignature))
            return rpcError(RpcSigningMalformed);

        // The Fee field must be in XRP and greater than zero.
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

    // Verify that the Signers field is present.
    if (!stTx->isFieldPresent(sfSigners))
        return RPC::missingFieldError("tx_json.Signers");

    // If the Signers field is present the SField guarantees it to be an array.
    // Get a reference to the Signers array so we can verify and sort it.
    auto& signers = stTx->peekFieldArray(sfSigners);

    if (signers.empty())
        return RPC::makeParamError("tx_json.Signers array may not be empty.");

    // The Signers array may only contain Signer objects.
    if (std::ranges::find_if_not(signers, [](STObject const& obj) {
            return (
                // A Signer object always contains these fields and no
                // others.
                obj.isFieldPresent(sfAccount) && obj.isFieldPresent(sfSigningPubKey) &&
                obj.isFieldPresent(sfTxnSignature) && obj.getCount() == 3);
        }) != signers.end())
    {
        return RPC::makeParamError("Signers array may only contain Signer entries.");
    }

    // The array must be sorted and validated.
    auto err = sortAndValidateSigners(signers, srcAddressID);
    if (RPC::containsError(err))
        return err;

    // Make sure the SerializedTransaction makes a legitimate Transaction.
    std::pair<json::Value, Transaction::pointer> txn =
        transactionConstructImpl(stTx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
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
