use std::ops::Deref;
use std::pin::Pin;
use cxx::{SharedPtr, UniquePtr};
use crate::{AccountID, ApplyFlags, LedgerSpecificFlags, NotTEC, rippled, TECcodes, TEFcodes, TEMcodes, TER, TEScodes};
use super::rippled::*;
// type PreflightContext = super::rippled::PreflightContext;
// type NotTEC = super::rippled::NotTEC;

pub fn pre_flight(ctx: &PreflightContext) -> NotTEC {
    let preflight1ret = preflight1(ctx);
    if preflight1ret != TEScodes::tesSUCCESS {
        return preflight1ret;
    }

    // There are no SetRegularKey tx flags, so if there are any flags set, return an error code
    let sttx_as_stobject = upcast(ctx.getTx());
    if (sttx_as_stobject.getFlags() & tfUniversalMask()) != 0 {
        println!("Malformed transaction: Invalid flags set.");
        return TEMcodes::temINVALID_FLAG.into();
    }

    let regular_key: AccountID = sttx_as_stobject.getAccountID(sfRegularKey());
    println!("RegularKey: {:?}", toBase58(&regular_key));
    println!("Account: {:?}", toBase58(&sttx_as_stobject.getAccountID(sfAccount())));
    if ctx.getRules().enabled(fixMasterKeyAsRegularKey()) &&
        sttx_as_stobject.isFieldPresent(sfRegularKey()) &&
        sttx_as_stobject.getAccountID(sfRegularKey()) == sttx_as_stobject.getAccountID(sfAccount()) {
        return TEMcodes::temBAD_REGKEY.into();
    }

    preflight2(ctx)
}

pub fn pre_claim(ctx: &PreclaimContext) -> TER {
    return TEScodes::tesSUCCESS.into();
}

// TODO: Write wrappers for everything used in this function in plugin-transactor/lib.rs,
//   then add `do_apply` to `transactor.rs` Transactor trait and implement on DummyTx using the
//   wrappers instead of raw ffi code.
//   Once that's done, we should get rid of this file.
pub fn do_apply(mut ctx: Pin<&mut ApplyContext>, m_prior_balance: XRPAmount, m_source_balance: XRPAmount) -> TER {
    let tx_as_stobject = upcast(ctx.getTx());
    let account = tx_as_stobject.getAccountID(sfAccount());
    let keylet = rippled::account(&account);

    let sle: SharedPtr<SLE> = ctx.as_mut().view().peek(&keylet);
    if sle.is_null() {
        return TEFcodes::tefINTERNAL.into();
    }

    let app: Pin<&mut Application> = ctx.as_mut().getApp();
    let base_fee: XRPAmount = ctx.as_mut().getBaseFee();
    let view: Pin<&mut ApplyView> = ctx.as_mut().view();
    let fees: &Fees = view.fees();
    let flags: ApplyFlags = view.flags();

    // This will never be true unless we duplicate the logic in SetRegularKey::calculateBaseFee
    if minimumFee(app, base_fee, fees, flags) == XRPAmount::zero() {
        setFlag(&sle, LedgerSpecificFlags::lsfPasswordSpent.into());
    }

    if upcast(ctx.getTx()).isFieldPresent(getSField(24, 1)) {
        // setPluginType(&sle, sfRegularKey(), upcast(ctx.getTx()).getPluginType(getSField(24, 1)));
        // TODO: This crashes rippled because we are trying to set AccountRoot.RegularKey to an STPluginType'd
        //   field, but AccountRoot.RegularKey is an STAccount field. In reality, you'd never try to do this so
        //   we're not spending any time right now to get it to work, but if you did want to, you'd have to
        //   somehow convert an STPluginType to an STAccount before setting the field.
        setPluginType(&sle, getSField(24, 1), upcast(ctx.getTx()).getPluginType(getSField(24, 1)));
    } else {
        if sle.deref().isFlag(LedgerSpecificFlags::lsfDisableMaster.into()) & !ctx.as_mut().view().peek(&signers(&account)).is_null() {
            return TECcodes::tecNO_ALTERNATIVE_KEY.into();
        }

        makeFieldAbsent(&sle,getSField(24, 1));
    }

    return TEScodes::tesSUCCESS.into();
}