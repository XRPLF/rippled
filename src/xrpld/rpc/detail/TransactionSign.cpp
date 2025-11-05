#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/DeliverMax.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/paths/Pathfinder.h>
#include <xrpld/app/tx/apply.h>  // Validity::Valid
#include <xrpld/app/tx/applySteps.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/TransactionSign.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <iterator>
#include <optional>

namespace ripple {
namespace RPC {
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

    SigningForParams(AccountID const& multiSigningAcctID)
        : multiSigningAcctID_(&multiSigningAcctID)
    {
    }

    bool
    isMultiSigning() const
    {
        return multiSigningAcctID_ != nullptr;
    }

    bool
    isSingleSigning() const
    {
        return !isMultiSigning();
    }

    // When multi-signing we should not edit the tx_json fields.
    bool
    editFields() const
    {
        return !isMultiSigning();
    }

    bool
    validMultiSign() const
    {
        return isMultiSigning() && multiSignPublicKey_ &&
            multiSignature_.size();
    }

    // Don't call this method unless isMultiSigning() returns true.
    AccountID const&
    getSigner() const
    {
        if (!multiSigningAcctID_)
            LogicError("Accessing unknown SigningForParams::getSigner()");
        return *multiSigningAcctID_;
    }

    PublicKey const&
    getPublicKey() const
    {
        if (!multiSignPublicKey_)
            LogicError("Accessing unknown SigningForParams::getPublicKey()");
        return *multiSignPublicKey_;
    }

    Buffer const&
    getSignature() const
    {
        return multiSignature_;
    }

    std::optional<std::reference_wrapper<SField const>> const&
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
    setSignatureTarget(
        std::optional<std::reference_wrapper<SField const>> const& field)
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

static error_code_i
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
            return rpcSUCCESS;
        return rpcBAD_SECRET;
    }

    // If we *can* get to the accountRoot, check for MASTER_DISABLED.
    auto const& sle = *accountState;
    if (isMasterKey)
    {
        if (sle.isFlag(lsfDisableMaster))
            return rpcMASTER_DISABLED;
        return rpcSUCCESS;
    }

    // The last gasp is that we have public Regular key.
    if ((sle.isFieldPresent(sfRegularKey)) &&
        (publicKeyAcctID == sle.getAccountID(sfRegularKey)))
    {
        return rpcSUCCESS;
    }
    return rpcBAD_SECRET;
}

static Json::Value
checkPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    AccountID const& srcAddressID,
    Role const role,
    Application& app,
    bool doPath)
{
    // Only path find for Payments.
    if (tx_json[jss::TransactionType].asString() != jss::Payment)
        return Json::Value();

    // DeliverMax is an alias to Amount and we use Amount internally
    if (tx_json.isMember(jss::DeliverMax))
    {
        if (tx_json.isMember(jss::Amount))
        {
            if (tx_json[jss::DeliverMax] != tx_json[jss::Amount])
                return RPC::make_error(
                    rpcINVALID_PARAMS,
                    "Cannot specify differing 'Amount' and 'DeliverMax'");
        }
        else
            tx_json[jss::Amount] = tx_json[jss::DeliverMax];

        tx_json.removeMember(jss::DeliverMax);
    }

    if (!tx_json.isMember(jss::Amount))
        return RPC::missing_field_error("tx_json.Amount");

    STAmount amount;

    if (!amountFromJsonNoThrow(amount, tx_json[jss::Amount]))
        return RPC::invalid_field_error("tx_json.Amount");

    if (!tx_json.isMember(jss::Destination))
        return RPC::missing_field_error("tx_json.Destination");

    auto const dstAccountID =
        parseBase58<AccountID>(tx_json[jss::Destination].asString());
    if (!dstAccountID)
        return RPC::invalid_field_error("tx_json.Destination");

    if (params.isMember(jss::build_path) &&
        ((doPath == false) || amount.holds<MPTIssue>()))
        return RPC::make_error(
            rpcINVALID_PARAMS,
            "Field 'build_path' not allowed in this context.");

    if (tx_json.isMember(jss::Paths) && params.isMember(jss::build_path))
        return RPC::make_error(
            rpcINVALID_PARAMS,
            "Cannot specify both 'tx_json.Paths' and 'build_path'");

    std::optional<uint256> domain;
    if (tx_json.isMember(sfDomainID.jsonName))
    {
        uint256 num;
        if (!tx_json[sfDomainID.jsonName].isString() ||
            !num.parseHex(tx_json[sfDomainID.jsonName].asString()))
        {
            return RPC::make_error(
                rpcDOMAIN_MALFORMED, "Unable to parse 'DomainID'.");
        }
        else
        {
            domain = num;
        }
    }

    if (!tx_json.isMember(jss::Paths) && params.isMember(jss::build_path))
    {
        STAmount sendMax;

        if (tx_json.isMember(jss::SendMax))
        {
            if (!amountFromJsonNoThrow(sendMax, tx_json[jss::SendMax]))
                return RPC::invalid_field_error("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer.
            sendMax = amount;
            sendMax.setIssuer(srcAddressID);
        }

        if (sendMax.native() && amount.native())
            return RPC::make_error(
                rpcINVALID_PARAMS, "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind lpf(isUnlimited(role), app);
            if (!lpf.isOk())
                return rpcError(rpcTOO_BUSY);

            STPathSet result;

            if (auto ledger = app.openLedger().current())
            {
                Pathfinder pf(
                    std::make_shared<RippleLineCache>(
                        ledger, app.journal("RippleLineCache")),
                    srcAddressID,
                    *dstAccountID,
                    sendMax.issue().currency,
                    sendMax.issue().account,
                    amount,
                    std::nullopt,
                    domain,
                    app);
                if (pf.findPaths(app.config().PATH_SEARCH_OLD))
                {
                    // 4 is the maxium paths
                    pf.computePathRanks(4);
                    STPath fullLiquidityPath;
                    STPathSet paths;
                    result = pf.getBestPaths(
                        4, fullLiquidityPath, paths, sendMax.issue().account);
                }
            }

            auto j = app.journal("RPCHandler");
            JLOG(j.debug()) << "transactionSign: build_path: "
                            << result.getJson(JsonOptions::none);

            if (!result.empty())
                tx_json[jss::Paths] = result.getJson(JsonOptions::none);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// Validate (but don't modify) the contents of the tx_json.
//
// Returns a pair<Json::Value, AccountID>.  The Json::Value will contain error
// information if there was an error. On success, the account ID is returned
// and the Json::Value will be empty.
//
// This code does not check the "Sequence" field, since the expectations
// for that field are particularly context sensitive.
static std::pair<Json::Value, AccountID>
checkTxJsonFields(
    Json::Value const& tx_json,
    Role const role,
    bool const verify,
    std::chrono::seconds validatedLedgerAge,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    unsigned apiVersion)
{
    std::pair<Json::Value, AccountID> ret;

    if (!tx_json.isObject())
    {
        ret.first = RPC::object_field_error(jss::tx_json);
        return ret;
    }

    if (!tx_json.isMember(jss::TransactionType))
    {
        ret.first = RPC::missing_field_error("tx_json.TransactionType");
        return ret;
    }

    if (!tx_json.isMember(jss::Account))
    {
        ret.first = RPC::make_error(
            rpcSRC_ACT_MISSING, RPC::missing_field_message("tx_json.Account"));
        return ret;
    }

    auto const srcAddressID =
        parseBase58<AccountID>(tx_json[jss::Account].asString());

    if (!srcAddressID)
    {
        ret.first = RPC::make_error(
            rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message("tx_json.Account"));
        return ret;
    }

    // Check for current ledger.
    if (verify && !config.standalone() &&
        (validatedLedgerAge > Tuning::maxValidatedLedgerAge))
    {
        if (apiVersion == 1)
            ret.first = rpcError(rpcNO_CURRENT);
        else
            ret.first = rpcError(rpcNOT_SYNCED);
        return ret;
    }

    // Check for load.
    if (feeTrack.isLoadedCluster() && !isUnlimited(role))
    {
        ret.first = rpcError(rpcTOO_BUSY);
        return ret;
    }

    // It's all good.  Return the AccountID.
    ret.second = *srcAddressID;
    return ret;
}

//------------------------------------------------------------------------------

// A move-only struct that makes it easy to return either a Json::Value or a
// std::shared_ptr<STTx const> from transactionPreProcessImpl ().
struct transactionPreProcessResult
{
    Json::Value const first;
    std::shared_ptr<STTx> const second;

    transactionPreProcessResult() = delete;
    transactionPreProcessResult(transactionPreProcessResult const&) = delete;
    transactionPreProcessResult(transactionPreProcessResult&& rhs) = default;

    transactionPreProcessResult&
    operator=(transactionPreProcessResult const&) = delete;
    transactionPreProcessResult&
    operator=(transactionPreProcessResult&&) = delete;

    transactionPreProcessResult(Json::Value&& json)
        : first(std::move(json)), second()
    {
    }

    explicit transactionPreProcessResult(std::shared_ptr<STTx>&& st)
        : first(), second(std::move(st))
    {
    }
};

static transactionPreProcessResult
transactionPreProcessImpl(
    Json::Value& params,
    Role role,
    SigningForParams& signingArgs,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    auto j = app.journal("RPCHandler");

    Json::Value jvResult;
    std::optional<std::pair<PublicKey, SecretKey>> keyPair =
        keypairForSignature(params, jvResult);
    if (!keyPair || contains_error(jvResult))
        return jvResult;

    PublicKey const& pk = keyPair->first;
    SecretKey const& sk = keyPair->second;

    bool const verify =
        !(params.isMember(jss::offline) && params[jss::offline].asBool());

    auto const signatureTarget =
        [&params]() -> std::optional<std::reference_wrapper<SField const>> {
        if (params.isMember(jss::signature_target))
            return SField::getField(params[jss::signature_target].asString());
        return std::nullopt;
    }();

    // Make sure the signature target field is valid, if specified, and save the
    // template for use later
    auto const signatureTemplate = signatureTarget
        ? InnerObjectFormats::getInstance().findSOTemplateBySField(
              *signatureTarget)
        : nullptr;
    if (signatureTarget)
    {
        if (!signatureTemplate)
        {  // Invalid target field
            return RPC::make_error(
                rpcINVALID_PARAMS, signatureTarget->get().getName());
        }
        signingArgs.setSignatureTarget(signatureTarget);
    }

    if (!params.isMember(jss::tx_json))
        return RPC::missing_field_error(jss::tx_json);

    Json::Value& tx_json(params[jss::tx_json]);

    // Check tx_json fields, but don't add any.
    auto [txJsonResult, srcAddressID] = checkTxJsonFields(
        tx_json,
        role,
        verify,
        validatedLedgerAge,
        app.config(),
        app.getFeeTrack(),
        getAPIVersionNumber(params, app.config().BETA_RPC_API));

    if (RPC::contains_error(txJsonResult))
        return std::move(txJsonResult);

    // This test covers the case where we're offline so the sequence number
    // cannot be determined locally.  If we're offline then the caller must
    // provide the sequence number.
    if (!verify && !tx_json.isMember(jss::Sequence))
        return RPC::missing_field_error("tx_json.Sequence");

    std::shared_ptr<SLE const> sle;
    if (verify)
        sle = app.openLedger().current()->read(keylet::account(srcAddressID));

    if (verify && !sle)
    {
        // If not offline and did not find account, error.
        JLOG(j.debug()) << "transactionSign: Failed to find source account "
                        << "in current ledger: " << toBase58(srcAddressID);

        return rpcError(rpcSRC_ACT_NOT_FOUND);
    }

    if (signingArgs.editFields())
    {
        if (!tx_json.isMember(jss::Sequence))
        {
            bool const hasTicketSeq =
                tx_json.isMember(sfTicketSequence.jsonName);
            if (!hasTicketSeq && !sle)
            {
                JLOG(j.debug())
                    << "transactionSign: Failed to find source account "
                    << "in current ledger: " << toBase58(srcAddressID);

                return rpcError(rpcSRC_ACT_NOT_FOUND);
            }
            tx_json[jss::Sequence] =
                hasTicketSeq ? 0 : app.getTxQ().nextQueuableSeq(sle).value();
        }

        if (!tx_json.isMember(jss::NetworkID))
        {
            auto const networkId = app.config().NETWORK_ID;
            if (networkId > 1024)
                tx_json[jss::NetworkID] = to_string(networkId);
        }
    }

    {
        Json::Value err = checkFee(
            params,
            role,
            verify && signingArgs.editFields(),
            app.config(),
            app.getFeeTrack(),
            app.getTxQ(),
            app);

        if (RPC::contains_error(err))
            return err;
    }

    {
        Json::Value err = checkPayment(
            params,
            tx_json,
            srcAddressID,
            role,
            app,
            verify && signingArgs.editFields());

        if (RPC::contains_error(err))
            return err;
    }

    // If multisigning there should not be a single signature and vice versa.
    if (signingArgs.isMultiSigning())
    {
        if (tx_json.isMember(jss::TxnSignature))
            return rpcError(rpcALREADY_SINGLE_SIG);

        // If multisigning then we need to return the public key.
        signingArgs.setPublicKey(pk);
    }
    else if (signingArgs.isSingleSigning())
    {
        if (tx_json.isMember(jss::Signers))
            return rpcError(rpcALREADY_MULTISIG);
    }

    if (verify)
    {
        if (!sle)
            // XXX Ignore transactions for accounts not created.
            return rpcError(rpcSRC_ACT_NOT_FOUND);

        JLOG(j.trace()) << "verify: " << toBase58(calcAccountID(pk)) << " : "
                        << toBase58(srcAddressID);

        // Don't do this test if multisigning or if the signature is going into
        // an alternate field since the account and secret probably don't belong
        // together in that case.
        if (!signingArgs.isMultiSigning() && !signatureTarget)
        {
            // Make sure the account and secret belong together.
            if (tx_json.isMember(sfDelegate.jsonName))
            {
                // Delegated transaction
                auto const delegateJson = tx_json[sfDelegate.jsonName];
                auto const ptrDelegatedAddressID = delegateJson.isString()
                    ? parseBase58<AccountID>(delegateJson.asString())
                    : std::nullopt;

                if (!ptrDelegatedAddressID)
                {
                    return RPC::make_error(
                        rpcSRC_ACT_MALFORMED,
                        RPC::invalid_field_message("tx_json.Delegate"));
                }

                auto delegatedAddressID = *ptrDelegatedAddressID;
                auto delegatedSle = app.openLedger().current()->read(
                    keylet::account(delegatedAddressID));
                if (!delegatedSle)
                    return rpcError(rpcDELEGATE_ACT_NOT_FOUND);

                auto const err =
                    acctMatchesPubKey(delegatedSle, delegatedAddressID, pk);

                if (err != rpcSUCCESS)
                    return rpcError(err);
            }
            else
            {
                auto const err = acctMatchesPubKey(sle, srcAddressID, pk);

                if (err != rpcSUCCESS)
                    return rpcError(err);
            }
        }
    }

    STParsedJSONObject parsed(std::string(jss::tx_json), tx_json);
    if (!parsed.object.has_value())
    {
        Json::Value err;
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
                parsed.object->setFieldObject(
                    *signatureTarget,
                    STObject{*signatureTemplate, *signatureTarget});
            sigObject = &parsed.object->peekFieldObject(*signatureTarget);
        }
        sigObject->setFieldVL(
            sfSigningPubKey,
            signingArgs.isMultiSigning() ? Slice(nullptr, 0) : pk.slice());

        stTx = std::make_shared<STTx>(std::move(parsed.object.value()));
    }
    catch (STObject::FieldErr& err)
    {
        return RPC::make_error(rpcINVALID_PARAMS, err.what());
    }
    catch (std::exception&)
    {
        return RPC::make_error(
            rpcINTERNAL,
            "Exception occurred constructing serialized transaction");
    }

    std::string reason;
    if (!passesLocalChecks(*stTx, reason))
        return RPC::make_error(rpcINVALID_PARAMS, reason);

    // If multisign then return multiSignature, else set TxnSignature field.
    if (signingArgs.isMultiSigning())
    {
        Serializer s = buildMultiSigningData(*stTx, signingArgs.getSigner());

        auto multisig = ripple::sign(pk, sk, s.slice());

        signingArgs.moveMultiSignature(std::move(multisig));
    }
    else if (signingArgs.isSingleSigning())
    {
        stTx->sign(pk, sk, signatureTarget);
    }

    return transactionPreProcessResult{std::move(stTx)};
}

static std::pair<Json::Value, Transaction::pointer>
transactionConstructImpl(
    std::shared_ptr<STTx const> const& stTx,
    Rules const& rules,
    Application& app)
{
    std::pair<Json::Value, Transaction::pointer> ret;

    // Turn the passed in STTx into a Transaction.
    Transaction::pointer tpTrans;
    {
        std::string reason;
        tpTrans = std::make_shared<Transaction>(stTx, reason, app);
        if (tpTrans->getStatus() != NEW)
        {
            ret.first = RPC::make_error(
                rpcINTERNAL, "Unable to construct transaction: " + reason);
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
            Blob transBlob = s.getData();
            SerialIter sit{makeSlice(transBlob)};

            // Check the signature if that's called for.
            auto sttxNew = std::make_shared<STTx const>(sit);
            if (!app.checkSigs())
                forceValidity(
                    app.getHashRouter(),
                    sttxNew->getTransactionID(),
                    Validity::SigGoodOnly);
            if (checkValidity(
                    app.getHashRouter(), *sttxNew, rules, app.config())
                    .first != Validity::Valid)
            {
                ret.first = RPC::make_error(rpcINTERNAL, "Invalid signature.");
                return ret;
            }

            std::string reason;
            auto tpTransNew =
                std::make_shared<Transaction>(sttxNew, reason, app);

            if (tpTransNew)
            {
                if (!tpTransNew->getSTransaction()->isEquivalent(
                        *tpTrans->getSTransaction()))
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
        ret.first =
            RPC::make_error(rpcINTERNAL, "Unable to sterilize transaction.");
        return ret;
    }
    ret.second = std::move(tpTrans);
    return ret;
}

static Json::Value
transactionFormatResultImpl(Transaction::pointer tpTrans, unsigned apiVersion)
{
    Json::Value jvResult;
    try
    {
        if (apiVersion > 1)
        {
            jvResult[jss::tx_json] =
                tpTrans->getJson(JsonOptions::disable_API_prior_V2);
            jvResult[jss::hash] = to_string(tpTrans->getID());
        }
        else
            jvResult[jss::tx_json] = tpTrans->getJson(JsonOptions::none);

        RPC::insertDeliverMax(
            jvResult[jss::tx_json],
            tpTrans->getSTransaction()->getTxnType(),
            apiVersion);

        jvResult[jss::tx_blob] =
            strHex(tpTrans->getSTransaction()->getSerializer().peekData());

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
        jvResult = RPC::make_error(
            rpcINTERNAL, "Exception occurred during JSON handling.");
    }
    return jvResult;
}

}  // namespace detail

//------------------------------------------------------------------------------

[[nodiscard]] static XRPAmount
getTxFee(Application const& app, Config const& config, Json::Value tx)
{
    auto const& ledger = app.openLedger().current();
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

        if (tx[jss::Signers].size() > STTx::maxMultiSigners(&ledger->rules()))
            return config.FEES.reference_fee;

        // check multi-signed signers
        for (auto& signer : tx[jss::Signers])
        {
            if (!signer.isMember(jss::Signer) ||
                !signer[jss::Signer].isObject())
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

        return calculateBaseFee(*app.openLedger().current(), stTx);
    }
    catch (std::exception& e)
    {
        return config.FEES.reference_fee;
    }
}

Json::Value
getCurrentNetworkFee(
    Role const role,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app,
    Json::Value const& tx,
    int mult,
    int div)
{
    XRPAmount const feeDefault = getTxFee(app, config, tx);

    auto ledger = app.openLedger().current();
    // Administrative and identified endpoints are exempt from local fees.
    XRPAmount const loadFee =
        scaleFeeLoad(feeDefault, feeTrack, ledger->fees(), isUnlimited(role));
    XRPAmount fee = loadFee;
    {
        auto const metrics = txQ.getMetrics(*ledger);
        auto const baseFee = ledger->fees().base;
        auto escalatedFee =
            toDrops(metrics.openLedgerFeeLevel - FeeLevel64(1), baseFee) + 1;
        fee = std::max(fee, escalatedFee);
    }

    auto const limit = mulDiv(feeDefault, mult, div);
    if (!limit)
        Throw<std::overflow_error>("mulDiv");

    if (fee > *limit)
    {
        std::stringstream ss;
        ss << "Fee of " << fee << " exceeds the requested tx limit of "
           << *limit;
        return RPC::make_error(rpcHIGH_FEE, ss.str());
    }

    return fee.jsonClipped();
}

Json::Value
checkFee(
    Json::Value& request,
    Role const role,
    bool doAutoFill,
    Config const& config,
    LoadFeeTrack const& feeTrack,
    TxQ const& txQ,
    Application const& app)
{
    Json::Value& tx(request[jss::tx_json]);
    if (tx.isMember(jss::Fee))
        return Json::Value();

    if (!doAutoFill)
        return RPC::missing_field_error("tx_json.Fee");

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    int div = Tuning::defaultAutoFillFeeDivisor;
    if (request.isMember(jss::fee_mult_max))
    {
        if (request[jss::fee_mult_max].isInt())
        {
            mult = request[jss::fee_mult_max].asInt();
            if (mult < 0)
                return RPC::make_error(
                    rpcINVALID_PARAMS,
                    RPC::expected_field_message(
                        jss::fee_mult_max, "a positive integer"));
        }
        else
        {
            return RPC::make_error(
                rpcHIGH_FEE,
                RPC::expected_field_message(
                    jss::fee_mult_max, "a positive integer"));
        }
    }
    if (request.isMember(jss::fee_div_max))
    {
        if (request[jss::fee_div_max].isInt())
        {
            div = request[jss::fee_div_max].asInt();
            if (div <= 0)
                return RPC::make_error(
                    rpcINVALID_PARAMS,
                    RPC::expected_field_message(
                        jss::fee_div_max, "a positive integer"));
        }
        else
        {
            return RPC::make_error(
                rpcHIGH_FEE,
                RPC::expected_field_message(
                    jss::fee_div_max, "a positive integer"));
        }
    }

    auto feeOrError =
        getCurrentNetworkFee(role, config, feeTrack, txQ, app, tx, mult, div);
    if (feeOrError.isMember(jss::error))
        return feeOrError;
    tx[jss::Fee] = std::move(feeOrError);
    return Json::Value();
}

//------------------------------------------------------------------------------

/** Returns a Json::objectValue. */
Json::Value
transactionSign(
    Json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    using namespace detail;

    auto j = app.journal("RPCHandler");
    JLOG(j.debug()) << "transactionSign: " << jvRequest;

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult = transactionPreProcessImpl(
        jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    std::shared_ptr<ReadView const> ledger = app.openLedger().current();
    // Make sure the STTx makes a legitimate Transaction.
    std::pair<Json::Value, Transaction::pointer> txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Returns a Json::objectValue. */
Json::Value
transactionSubmit(
    Json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    using namespace detail;

    auto const& ledger = app.openLedger().current();
    auto j = app.journal("RPCHandler");
    JLOG(j.debug()) << "transactionSubmit: " << jvRequest;

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams;
    transactionPreProcessResult preprocResult = transactionPreProcessImpl(
        jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    // Make sure the STTx makes a legitimate Transaction.
    std::pair<Json::Value, Transaction::pointer> txn =
        transactionConstructImpl(preprocResult.second, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        processTransaction(txn.second, isUnlimited(role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error(
            rpcINTERNAL, "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl(txn.second, apiVersion);
}

namespace detail {
// There are a some field checks shared by transactionSignFor
// and transactionSubmitMultiSigned.  Gather them together here.
static Json::Value
checkMultiSignFields(Json::Value const& jvRequest)
{
    if (!jvRequest.isMember(jss::tx_json))
        return RPC::missing_field_error(jss::tx_json);

    Json::Value const& tx_json(jvRequest[jss::tx_json]);

    if (!tx_json.isObject())
        return RPC::invalid_field_message(jss::tx_json);

    // There are a couple of additional fields we need to check before
    // we serialize.  If we serialize first then we generate less useful
    // error messages.
    if (!tx_json.isMember(jss::Sequence))
        return RPC::missing_field_error("tx_json.Sequence");

    if (!tx_json.isMember(sfSigningPubKey.getJsonName()))
        return RPC::missing_field_error("tx_json.SigningPubKey");

    if (!tx_json[sfSigningPubKey.getJsonName()].asString().empty())
        return RPC::make_error(
            rpcINVALID_PARAMS,
            "When multi-signing 'tx_json.SigningPubKey' must be empty.");

    return Json::Value();
}

// Sort and validate an stSigners array.
//
// Returns a null Json::Value if there are no errors.
static Json::Value
sortAndValidateSigners(STArray& signers, AccountID const& signingForID)
{
    if (signers.empty())
        return RPC::make_param_error("Signers array may not be empty.");

    // Signers must be sorted by Account.
    std::sort(
        signers.begin(),
        signers.end(),
        [](STObject const& a, STObject const& b) {
            return (a[sfAccount] < b[sfAccount]);
        });

    // Signers may not contain any duplicates.
    auto const dupIter = std::adjacent_find(
        signers.begin(),
        signers.end(),
        [](STObject const& a, STObject const& b) {
            return (a[sfAccount] == b[sfAccount]);
        });

    if (dupIter != signers.end())
    {
        std::ostringstream err;
        err << "Duplicate Signers:Signer:Account entries ("
            << toBase58((*dupIter)[sfAccount]) << ") are not allowed.";
        return RPC::make_param_error(err.str());
    }

    // An account may not sign for itself.
    if (signers.end() !=
        std::find_if(
            signers.begin(),
            signers.end(),
            [&signingForID](STObject const& elem) {
                return elem[sfAccount] == signingForID;
            }))
    {
        std::ostringstream err;
        err << "A Signer may not be the transaction's Account ("
            << toBase58(signingForID) << ").";
        return RPC::make_param_error(err.str());
    }
    return {};
}

}  // namespace detail

/** Returns a Json::objectValue. */
Json::Value
transactionSignFor(
    Json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app)
{
    auto const& ledger = app.openLedger().current();
    auto j = app.journal("RPCHandler");
    JLOG(j.debug()) << "transactionSignFor: " << jvRequest;

    // Verify presence of the signer's account field.
    char const accountField[] = "account";

    if (!jvRequest.isMember(accountField))
        return RPC::missing_field_error(accountField);

    // Turn the signer's account into an AccountID for multi-sign.
    auto const signerAccountID =
        parseBase58<AccountID>(jvRequest[accountField].asString());
    if (!signerAccountID)
    {
        return RPC::make_error(
            rpcSRC_ACT_MALFORMED, RPC::invalid_field_message(accountField));
    }

    if (!jvRequest.isMember(jss::tx_json))
        return RPC::missing_field_error(jss::tx_json);

    {
        Json::Value& tx_json(jvRequest[jss::tx_json]);

        if (!tx_json.isObject())
            return RPC::object_field_error(jss::tx_json);

        // If the tx_json.SigningPubKey field is missing,
        // insert an empty one.
        if (!tx_json.isMember(sfSigningPubKey.getJsonName()))
            tx_json[sfSigningPubKey.getJsonName()] = "";
    }

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields(jvRequest);
        if (RPC::contains_error(err))
            return err;
    }

    // Add and amend fields based on the transaction type.
    SigningForParams signForParams(*signerAccountID);

    transactionPreProcessResult preprocResult = transactionPreProcessImpl(
        jvRequest, role, signForParams, validatedLedgerAge, app);

    if (!preprocResult.second)
        return preprocResult.first;

    XRPL_ASSERT(
        signForParams.validMultiSign(),
        "ripple::RPC::transactionSignFor : valid multi-signature");

    {
        std::shared_ptr<SLE const> account_state =
            ledger->read(keylet::account(*signerAccountID));
        // Make sure the account and secret belong together.
        auto const err = acctMatchesPubKey(
            account_state, *signerAccountID, signForParams.getPublicKey());

        if (err != rpcSUCCESS)
            return rpcError(err);
    }

    // Inject the newly generated signature into tx_json.Signers.
    auto& sttx = preprocResult.second;
    {
        // Make the signer object that we'll inject.
        STObject signer = STObject::makeInnerObject(sfSigner);
        signer[sfAccount] = *signerAccountID;
        signer.setFieldVL(sfTxnSignature, signForParams.getSignature());
        signer.setFieldVL(
            sfSigningPubKey, signForParams.getPublicKey().slice());

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
        signers.emplace_back(std::move(signer));

        // The array must be sorted and validated.
        auto err = sortAndValidateSigners(signers, (*sttx)[sfAccount]);
        if (RPC::contains_error(err))
            return err;
    }

    // Make sure the STTx makes a legitimate Transaction.
    std::pair<Json::Value, Transaction::pointer> txn =
        transactionConstructImpl(sttx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    return transactionFormatResultImpl(txn.second, apiVersion);
}

/** Returns a Json::objectValue. */
Json::Value
transactionSubmitMultiSigned(
    Json::Value jvRequest,
    unsigned apiVersion,
    NetworkOPs::FailHard failType,
    Role role,
    std::chrono::seconds validatedLedgerAge,
    Application& app,
    ProcessTransactionFn const& processTransaction)
{
    auto const& ledger = app.openLedger().current();
    auto j = app.journal("RPCHandler");
    JLOG(j.debug()) << "transactionSubmitMultiSigned: " << jvRequest;

    // When multi-signing, the "Sequence" and "SigningPubKey" fields must
    // be passed in by the caller.
    using namespace detail;
    {
        Json::Value err = checkMultiSignFields(jvRequest);
        if (RPC::contains_error(err))
            return err;
    }

    Json::Value& tx_json(jvRequest["tx_json"]);

    auto [txJsonResult, srcAddressID] = checkTxJsonFields(
        tx_json,
        role,
        true,
        validatedLedgerAge,
        app.config(),
        app.getFeeTrack(),
        getAPIVersionNumber(jvRequest, app.config().BETA_RPC_API));

    if (RPC::contains_error(txJsonResult))
        return std::move(txJsonResult);

    std::shared_ptr<SLE const> sle =
        ledger->read(keylet::account(srcAddressID));

    if (!sle)
    {
        // If did not find account, error.
        JLOG(j.debug())
            << "transactionSubmitMultiSigned: Failed to find source account "
            << "in current ledger: " << toBase58(srcAddressID);

        return rpcError(rpcSRC_ACT_NOT_FOUND);
    }

    {
        Json::Value err = checkFee(
            jvRequest,
            role,
            false,
            app.config(),
            app.getFeeTrack(),
            app.getTxQ(),
            app);

        if (RPC::contains_error(err))
            return err;

        err = checkPayment(jvRequest, tx_json, srcAddressID, role, app, false);

        if (RPC::contains_error(err))
            return err;
    }

    // Grind through the JSON in tx_json to produce a STTx.
    std::shared_ptr<STTx> stTx;
    {
        STParsedJSONObject parsedTx_json("tx_json", tx_json);
        if (!parsedTx_json.object)
        {
            Json::Value jvResult;
            jvResult["error"] = parsedTx_json.error["error"];
            jvResult["error_code"] = parsedTx_json.error["error_code"];
            jvResult["error_message"] = parsedTx_json.error["error_message"];
            return jvResult;
        }
        try
        {
            stTx =
                std::make_shared<STTx>(std::move(parsedTx_json.object.value()));
        }
        catch (STObject::FieldErr& err)
        {
            return RPC::make_error(rpcINVALID_PARAMS, err.what());
        }
        catch (std::exception& ex)
        {
            std::string reason(ex.what());
            return RPC::make_error(
                rpcINTERNAL,
                "Exception while serializing transaction: " + reason);
        }
        std::string reason;
        if (!passesLocalChecks(*stTx, reason))
            return RPC::make_error(rpcINVALID_PARAMS, reason);
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
            return RPC::make_error(rpcINVALID_PARAMS, err.str());
        }

        // There may not be a TxnSignature field.
        if (stTx->isFieldPresent(sfTxnSignature))
            return rpcError(rpcSIGNING_MALFORMED);

        // The Fee field must be in XRP and greater than zero.
        auto const fee = stTx->getFieldAmount(sfFee);

        if (!isLegalNet(fee))
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Fees must be specified in XRP.";
            return RPC::make_error(rpcINVALID_PARAMS, err.str());
        }
        if (fee <= STAmount{0})
        {
            std::ostringstream err;
            err << "Invalid " << sfFee.fieldName
                << " field.  Fees must be greater than zero.";
            return RPC::make_error(rpcINVALID_PARAMS, err.str());
        }
    }

    // Verify that the Signers field is present.
    if (!stTx->isFieldPresent(sfSigners))
        return RPC::missing_field_error("tx_json.Signers");

    // If the Signers field is present the SField guarantees it to be an array.
    // Get a reference to the Signers array so we can verify and sort it.
    auto& signers = stTx->peekFieldArray(sfSigners);

    if (signers.empty())
        return RPC::make_param_error("tx_json.Signers array may not be empty.");

    // The Signers array may only contain Signer objects.
    if (std::find_if_not(
            signers.begin(), signers.end(), [](STObject const& obj) {
                return (
                    // A Signer object always contains these fields and no
                    // others.
                    obj.isFieldPresent(sfAccount) &&
                    obj.isFieldPresent(sfSigningPubKey) &&
                    obj.isFieldPresent(sfTxnSignature) && obj.getCount() == 3);
            }) != signers.end())
    {
        return RPC::make_param_error(
            "Signers array may only contain Signer entries.");
    }

    // The array must be sorted and validated.
    auto err = sortAndValidateSigners(signers, srcAddressID);
    if (RPC::contains_error(err))
        return err;

    // Make sure the SerializedTransaction makes a legitimate Transaction.
    std::pair<Json::Value, Transaction::pointer> txn =
        transactionConstructImpl(stTx, ledger->rules(), app);

    if (!txn.second)
        return txn.first;

    // Finally, submit the transaction.
    try
    {
        // FIXME: For performance, should use asynch interface
        processTransaction(txn.second, isUnlimited(role), true, failType);
    }
    catch (std::exception&)
    {
        return RPC::make_error(
            rpcINTERNAL, "Exception occurred during transaction submission.");
    }

    return transactionFormatResultImpl(txn.second, apiVersion);
}

}  // namespace RPC
}  // namespace ripple
