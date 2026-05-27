#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use crate::host::{Error, Result, Result::Err, Result::Ok};
use xrpl_std::core::keylets;
use xrpl_std::core::ledger_objects::current_escrow::get_current_escrow;
use xrpl_std::core::ledger_objects::current_escrow::CurrentEscrow;
use xrpl_std::core::ledger_objects::ledger_object;
use xrpl_std::core::ledger_objects::traits::CurrentEscrowFields;
use xrpl_std::core::ledger_objects::LedgerObjectFieldGetter;
use xrpl_std::core::types::currency::Currency;
use xrpl_std::core::types::issue::{IouIssue, Issue, XrpIssue};
use xrpl_std::core::types::mpt_id::MptId;
use xrpl_std::host;
use xrpl_std::host::trace::{trace, trace_acct, trace_data, trace_num, DataRepr};
use xrpl_std::sfield;

pub fn object_exists<T: LedgerObjectFieldGetter, const CODE: i32>(
    keylet_result: Result<keylets::KeyletBytes>,
    keylet_type: &str,
    sfield: sfield::SField<T, CODE>,
) -> Result<bool> {
    let field = CODE;
    match keylet_result {
        Ok(keylet) => {
            let _ = trace_data(keylet_type, &keylet, DataRepr::AsHex);

            let slot = unsafe { host::cache_le(keylet.as_ptr(), keylet.len(), 0) };
            if slot <= 0 {
                let _ = trace_num("Error: ", slot.into());
                return Err(Error::from_code(slot));
            }
            if field == 0 {
                let new_field = sfield::PreviousTxnID;
                let _ = trace_num("Getting field: ", new_field.clone().into());
                match ledger_object::get_field(slot, new_field) {
                    Ok(data) => {
                        let _ = trace_data("Field data: ", &data.0, DataRepr::AsHex);
                    }
                    Err(result_code) => {
                        let _ = trace_num("Error getting field: ", result_code.into());
                        return Err(result_code);
                    }
                }
            } else {
                let _ = trace_num("Getting field: ", field.into());
                match ledger_object::get_field(slot, sfield) {
                    Ok(_data) => {
                        let _ = trace("Field data: retrieved");
                    }
                    Err(result_code) => {
                        let _ = trace_num("Error getting field: ", result_code.into());
                        return Err(result_code);
                    }
                }
            }

            Ok(true)
        }
        Err(error) => {
            let _ = trace_num("Error getting keylet: ", error.into());
            Err(error)
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    let _ = trace("$$$$$ STARTING WASM EXECUTION $$$$$");

    let escrow: CurrentEscrow = get_current_escrow();

    let account = escrow.get_account().unwrap_or_panic();
    let _ = trace_acct("Account:", &account);

    let destination = escrow.get_destination().unwrap_or_panic();
    let _ = trace_acct("Destination:", &destination);

    let mut seq = 5;

    macro_rules! check_object_exists {
        ($keylet:expr, $type:expr, $field:expr) => {
            match object_exists($keylet, $type, $field) {
                Ok(_exists) => {
                    // false isn't returned
                    let _ = trace(concat!(
                        $type,
                        " object exists, proceeding with escrow finish."
                    ));
                }
                Err(error) => {
                    let _ = trace_num("Current seq value:", seq.try_into().unwrap());
                    return error.code();
                }
            }
        };
    }

    let accountroot_id = keylets::accountroot_id(&account);
    check_object_exists!(accountroot_id, "Account", sfield::Account);

    let currency_code: &[u8; 3] = b"USD";
    let currency: Currency = Currency::from(*currency_code);
    let trustline_id = keylets::trustline_id(&account, &destination, &currency);
    check_object_exists!(trustline_id, "Trustline", sfield::Generic);
    seq += 1;

    let asset1 = Issue::XRP(XrpIssue {});
    let asset2 = Issue::IOU(IouIssue::new(destination, currency));
    check_object_exists!(keylets::amm_id(&asset1, &asset2), "AMM", sfield::Account);

    let check_id = keylets::check_id(&account, seq);
    check_object_exists!(check_id, "Check", sfield::Account);
    seq += 1;

    let cred_type: &[u8] = b"termsandconditions";
    let credential_id = keylets::credential_id(&account, &account, cred_type);
    check_object_exists!(credential_id, "Credential", sfield::Subject);
    seq += 1;

    let delegate_id = keylets::delegate_id(&account, &destination);
    check_object_exists!(delegate_id, "Delegate", sfield::Account);
    seq += 1;

    let deposit_preauth_id = keylets::deposit_preauth_id(&account, &destination);
    check_object_exists!(deposit_preauth_id, "DepositPreauth", sfield::Account);
    seq += 1;

    let did_id = keylets::did_id(&account);
    check_object_exists!(did_id, "DID", sfield::Account);
    seq += 1;

    let escrow_id = keylets::escrow_id(&account, seq);
    check_object_exists!(escrow_id, "Escrow", sfield::Account);
    seq += 1;

    let mpt_issuance_id = keylets::mpt_issuance_id(&account, seq);
    let mpt_id = MptId::new(seq.try_into().unwrap(), account);
    check_object_exists!(mpt_issuance_id, "MPTIssuance", sfield::Issuer);
    seq += 1;

    let mptoken_id = keylets::mptoken_id(&mpt_id, &destination);
    check_object_exists!(mptoken_id, "MPToken", sfield::Account);

    let nft_offer_id = keylets::nft_offer_id(&destination, 6);
    check_object_exists!(nft_offer_id, "NFTokenOffer", sfield::Owner);

    let offer_id = keylets::offer_id(&account, seq);
    check_object_exists!(offer_id, "Offer", sfield::Account);
    seq += 1;

    let paychan_id = keylets::paychan_id(&account, &destination, seq);
    check_object_exists!(paychan_id, "PayChannel", sfield::Account);
    seq += 1;

    let pd_id = keylets::permissioned_domain_id(&account, seq);
    check_object_exists!(pd_id, "PermissionedDomain", sfield::Owner);
    seq += 1;

    let signers_id = keylets::signers_id(&account);
    check_object_exists!(signers_id, "SignerList", sfield::Generic);
    seq += 1;

    seq += 1; // ticket sequence number is one greater
    let ticket_id = keylets::ticket_id(&account, seq);
    check_object_exists!(ticket_id, "Ticket", sfield::Account);
    seq += 1;

    let vault_id = keylets::vault_id(&account, seq);
    check_object_exists!(vault_id, "Vault", sfield::Account);
    // seq += 1;

    1 // All keylets exist, finish the escrow.
}
