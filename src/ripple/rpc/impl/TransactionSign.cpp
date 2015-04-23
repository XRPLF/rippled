//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_reader.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/rpc/impl/KeypairForSignature.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <beast/unit_test/suite.h>

namespace ripple {

//------------------------------------------------------------------------------

namespace RPC {
namespace RPCDetail {

// LedgerFacade methods

void LedgerFacade::snapshotAccountState (RippleAddress const& accountID)
{
    if (!netOPs_) // Unit testing.
        return;

    ledger_ = netOPs_->getCurrentLedger ();
    accountID_ = accountID;
    accountState_ = netOPs_->getAccountState (ledger_, accountID_);
}

bool LedgerFacade::isValidAccount () const
{
    if (!ledger_) // Unit testing.
        return true;

    return static_cast <bool> (accountState_);
}

std::uint32_t LedgerFacade::getSeq () const
{
    if (!ledger_) // Unit testing.
        return 0;

    return accountState_->getSeq ();
}

Transaction::pointer LedgerFacade::submitTransactionSync (
    Transaction::ref tpTrans,
    bool bAdmin,
    bool bLocal,
    bool bFailHard,
    bool bSubmit)
{
    if (!netOPs_) // Unit testing.
        return tpTrans;

    return netOPs_->submitTransactionSync (
        tpTrans, bAdmin, bLocal, bFailHard, bSubmit);
}

bool LedgerFacade::findPathsForOneIssuer (
    RippleAddress const& dstAccountID,
    Issue const& srcIssue,
    STAmount const& dstAmount,
    int searchLevel,
    unsigned int const maxPaths,
    STPathSet& pathsOut,
    STPath& fullLiquidityPath) const
{
    if (!ledger_) // Unit testing.
        // Note that unit tests don't (yet) need pathsOut or fullLiquidityPath.
        return true;

    auto cache = std::make_shared<RippleLineCache> (ledger_);
    return ripple::findPathsForOneIssuer (
        cache,
        accountID_.getAccountID (),
        dstAccountID.getAccountID (),
        srcIssue,
        dstAmount,
        searchLevel,
        maxPaths,
        pathsOut,
        fullLiquidityPath);
}

std::uint64_t LedgerFacade::scaleFeeBase (std::uint64_t fee) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeBase (fee);
}

std::uint64_t LedgerFacade::scaleFeeLoad (std::uint64_t fee, bool bAdmin) const
{
    if (!ledger_) // Unit testing.
        return fee;

    return ledger_->scaleFeeLoad (fee, bAdmin);
}

bool LedgerFacade::hasAccountRoot () const
{
    if (!netOPs_) // Unit testing.
        return true;

    SLE::pointer const sleAccountRoot =
        ledger_->getSLEi(getAccountRootIndex(accountID_));

    return static_cast <bool> (sleAccountRoot);
}

bool LedgerFacade::accountMasterDisabled () const
{
    if (!accountState_) // Unit testing.
        return false;

    STLedgerEntry const& sle = accountState_->peekSLE ();
    return sle.isFlag(lsfDisableMaster);
}

bool LedgerFacade::accountMatchesRegularKey (Account account) const
{
    if (!accountState_) // Unit testing.
        return true;

    STLedgerEntry const& sle = accountState_->peekSLE ();
    return ((sle.isFieldPresent (sfRegularKey)) &&
        (account == sle.getFieldAccount160 (sfRegularKey)));
}

int LedgerFacade::getValidatedLedgerAge () const
{
    if (!netOPs_) // Unit testing.
        return 0;

    return getApp( ).getLedgerMaster ().getValidatedLedgerAge ();
}

bool LedgerFacade::isLoadedCluster () const
{
    if (!netOPs_) // Unit testing.
        return false;

    return getApp().getFeeTrack().isLoadedCluster();
}
} // namespace RPCDetail

//------------------------------------------------------------------------------

/** Fill in the fee on behalf of the client.
    This is called when the client does not explicitly specify the fee.
    The client may also put a ceiling on the amount of the fee. This ceiling
    is expressed as a multiplier based on the current ledger's fee schedule.

    JSON fields

    "Fee"   The fee paid by the transaction. Omitted when the client
            wants the fee filled in.

    "fee_mult_max"  A multiplier applied to the current ledger's transaction
                    fee that caps the maximum the fee server should auto fill.
                    If this optional field is not specified, then a default
                    multiplier is used.

    @param tx       The JSON corresponding to the transaction to fill in
    @param ledger   A ledger for retrieving the current fee schedule
    @param result   A JSON object for injecting error results, if any
    @param admin    `true` if this is called by an administrative endpoint.
*/
static void autofill_fee (
    Json::Value& request,
    RPCDetail::LedgerFacade& ledgerFacade,
    Json::Value& result,
    bool admin)
{
    Json::Value& tx (request[jss::tx_json]);
    if (tx.isMember (jss::Fee))
        return;

    int mult = Tuning::defaultAutoFillFeeMultiplier;
    if (request.isMember (jss::fee_mult_max))
    {
        if (request[jss::fee_mult_max].isNumeric ())
        {
            mult = request[jss::fee_mult_max].asInt();
        }
        else
        {
            RPC::inject_error (rpcHIGH_FEE, RPC::expected_field_message (
                jss::fee_mult_max, "a number"), result);
            return;
        }
    }

    // Default fee in fee units.
    std::uint64_t const feeDefault = getConfig().TRANSACTION_FEE_BASE;

    // Administrative endpoints are exempt from local fees.
    std::uint64_t const fee = ledgerFacade.scaleFeeLoad (feeDefault, admin);
    std::uint64_t const limit = mult * ledgerFacade.scaleFeeBase (feeDefault);

    if (fee > limit)
    {
        std::stringstream ss;
        ss <<
            "Fee of " << fee <<
            " exceeds the requested tx limit of " << limit;
        RPC::inject_error (rpcHIGH_FEE, ss.str(), result);
        return;
    }

    tx [jss::Fee] = static_cast<int>(fee);
}

static Json::Value signPayment(
    Json::Value const& params,
    Json::Value& tx_json,
    RippleAddress const& raSrcAddressID,
    RPCDetail::LedgerFacade& ledgerFacade,
    Role role)
{
    RippleAddress dstAccountID;

    if (!tx_json.isMember (jss::Amount))
        return RPC::missing_field_error ("tx_json.Amount");

    STAmount amount;

    if (! amountFromJsonNoThrow (amount, tx_json [jss::Amount]))
        return RPC::invalid_field_error ("tx_json.Amount");

    if (!tx_json.isMember (jss::Destination))
        return RPC::missing_field_error ("tx_json.Destination");

    if (!dstAccountID.setAccountID (tx_json[jss::Destination].asString ()))
        return RPC::invalid_field_error ("tx_json.Destination");

    if (tx_json.isMember (jss::Paths) && params.isMember (jss::build_path))
        return RPC::make_error (rpcINVALID_PARAMS,
            "Cannot specify both 'tx_json.Paths' and 'build_path'");

    if (!tx_json.isMember (jss::Paths)
        && tx_json.isMember (jss::Amount)
        && params.isMember (jss::build_path))
    {
        // Need a ripple path.
        Currency uSrcCurrencyID;
        Account uSrcIssuerID;

        STAmount    saSendMax;

        if (tx_json.isMember (jss::SendMax))
        {
            if (! amountFromJsonNoThrow (saSendMax, tx_json [jss::SendMax]))
                return RPC::invalid_field_error ("tx_json.SendMax");
        }
        else
        {
            // If no SendMax, default to Amount with sender as issuer.
            saSendMax = amount;
            saSendMax.setIssuer (raSrcAddressID.getAccountID ());
        }

        if (saSendMax.isNative () && amount.isNative ())
            return RPC::make_error (rpcINVALID_PARAMS,
                "Cannot build XRP to XRP paths.");

        {
            LegacyPathFind lpf (role == Role::ADMIN);
            if (!lpf.isOk ())
                return rpcError (rpcTOO_BUSY);

            STPathSet spsPaths;
            STPath fullLiquidityPath;
            bool valid = ledgerFacade.findPathsForOneIssuer (
                dstAccountID,
                saSendMax.issue (),
                amount,
                getConfig ().PATH_SEARCH_OLD,
                4,  // iMaxPaths
                spsPaths,
                fullLiquidityPath);


            if (!valid)
            {
                WriteLog (lsDEBUG, RPCHandler)
                        << "transactionSign: build_path: No paths found.";
                return rpcError (rpcNO_PATH);
            }
            WriteLog (lsDEBUG, RPCHandler)
                    << "transactionSign: build_path: "
                    << spsPaths.getJson (0);

            if (!spsPaths.empty ())
                tx_json[jss::Paths] = spsPaths.getJson (0);
        }
    }
    return Json::Value();
}

//------------------------------------------------------------------------------

// VFALCO TODO This function should take a reference to the params, modify it
//             as needed, and then there should be a separate function to
//             submit the transaction.
//
Json::Value
transactionSign (
    Json::Value params,
    bool bSubmit,
    bool bFailHard,
    RPCDetail::LedgerFacade& ledgerFacade,
    Role role)
{
    Json::Value jvResult;

    WriteLog (lsDEBUG, RPCHandler) << "transactionSign: " << params;

    KeyPair const keypair = keypairForSignature (params, jvResult);

    if (contains_error (jvResult))
    {
        return jvResult;
    }

    if (! params.isMember (jss::tx_json))
        return RPC::missing_field_error (jss::tx_json);


    Json::Value& tx_json (params [jss::tx_json]);

    if (! tx_json.isObject ())
        return RPC::object_field_error (jss::tx_json);

    if (! tx_json.isMember (jss::TransactionType))
        return RPC::missing_field_error ("tx_json.TransactionType");

    std::string const sType = tx_json [jss::TransactionType].asString ();

    if (! tx_json.isMember (jss::Account))
        return RPC::make_error (rpcSRC_ACT_MISSING,
            RPC::missing_field_message ("tx_json.Account"));

    RippleAddress raSrcAddressID;

    if (! raSrcAddressID.setAccountID (tx_json[jss::Account].asString ()))
        return RPC::make_error (rpcSRC_ACT_MALFORMED,
            RPC::invalid_field_message ("tx_json.Account"));

    bool const verify = !(params.isMember (jss::offline)
                          && params[jss::offline].asBool ());

    if (!tx_json.isMember (jss::Sequence) && !verify)
        return RPC::missing_field_error ("tx_json.Sequence");

    // Check for current ledger.
    if (verify && !getConfig ().RUN_STANDALONE &&
        (ledgerFacade.getValidatedLedgerAge () > 120))
        return rpcError (rpcNO_CURRENT);

    // Check for load.
    if (ledgerFacade.isLoadedCluster () && (role != Role::ADMIN))
        return rpcError (rpcTOO_BUSY);

    ledgerFacade.snapshotAccountState (raSrcAddressID);

    if (verify) {
        if (!ledgerFacade.isValidAccount ())
        {
            // If not offline and did not find account, error.
            WriteLog (lsDEBUG, RPCHandler)
                << "transactionSign: Failed to find source account "
                << "in current ledger: "
                << raSrcAddressID.humanAccountID ();

            return rpcError (rpcSRC_ACT_NOT_FOUND);
        }
    }

    autofill_fee (params, ledgerFacade, jvResult, role == Role::ADMIN);
    if (RPC::contains_error (jvResult))
        return jvResult;

    if ("Payment" == sType)
    {
        auto e = signPayment(
            params,
            tx_json,
            raSrcAddressID,
            ledgerFacade,
            role);
        if (contains_error(e))
            return e;
    }

    if (!tx_json.isMember (jss::Sequence))
        tx_json[jss::Sequence] = ledgerFacade.getSeq ();

    if (!tx_json.isMember (jss::Flags))
        tx_json[jss::Flags] = tfFullyCanonicalSig;

    if (verify)
    {
        if (!ledgerFacade.hasAccountRoot ())
            // XXX Ignore transactions for accounts not created.
            return rpcError (rpcSRC_ACT_NOT_FOUND);
    }

    if (verify)
    {
        WriteLog (lsTRACE, RPCHandler) <<
                "verify: " << keypair.publicKey.humanAccountID() <<
                " : " << raSrcAddressID.humanAccountID ();

        auto const secretAccountID = keypair.publicKey.getAccountID();
        if (raSrcAddressID.getAccountID () == secretAccountID)
        {
            if (ledgerFacade.accountMasterDisabled ())
                return rpcError (rpcMASTER_DISABLED);
        }
        else if (!ledgerFacade.accountMatchesRegularKey (secretAccountID))
        {
            return rpcError (rpcBAD_SECRET);
        }
    }

    STParsedJSONObject parsed (std::string (jss::tx_json), tx_json);
    if (! parsed.object)
    {
        jvResult [jss::error] = parsed.error [jss::error];
        jvResult [jss::error_code] = parsed.error [jss::error_code];
        jvResult [jss::error_message] = parsed.error [jss::error_message];
        return jvResult;
    }
    parsed.object->setFieldVL (
        sfSigningPubKey,
        keypair.publicKey.getAccountPublic());

    STTx::pointer stpTrans;

    try
    {
        stpTrans = std::make_shared<STTx> (std::move (*parsed.object));
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction");
    }

    std::string reason;
    if (!passesLocalChecks (*stpTrans, reason))
        return RPC::make_error (rpcINVALID_PARAMS, reason);

    if (params.isMember (jss::debug_signing))
    {
        jvResult[jss::tx_unsigned] = strHex (
            stpTrans->getSerializer ().peekData ());
        jvResult[jss::tx_signing_hash] = to_string (stpTrans->getSigningHash ());
    }

    // FIXME: For performance, transactions should not be signed in this code
    // path.

    stpTrans->sign (keypair.secretKey);

    Transaction::pointer tpTrans = std::make_shared<Transaction> (stpTrans,
        Validate::NO, reason);

    try
    {
        // FIXME: For performance, should use asynch interface.
        tpTrans = ledgerFacade.submitTransactionSync (tpTrans,
            role == Role::ADMIN, true, bFailHard, bSubmit);

        if (!tpTrans)
        {
            return RPC::make_error (rpcINTERNAL,
                "Unable to sterilize transaction.");
        }
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during transaction submission.");
    }

    try
    {
        jvResult[jss::tx_json] = tpTrans->getJson (0);
        jvResult[jss::tx_blob] = strHex (
            tpTrans->getSTransaction ()->getSerializer ().peekData ());

        if (temUNCERTAIN != tpTrans->getResult ())
        {
            std::string sToken;
            std::string sHuman;

            transResultInfo (tpTrans->getResult (), sToken, sHuman);

            jvResult[jss::engine_result]           = sToken;
            jvResult[jss::engine_result_code]      = tpTrans->getResult ();
            jvResult[jss::engine_result_message]   = sHuman;
        }

        return jvResult;
    }
    catch (std::exception&)
    {
        return RPC::make_error (rpcINTERNAL,
            "Exception occurred during JSON handling.");
    }
}

} // RPC
} // ripple
