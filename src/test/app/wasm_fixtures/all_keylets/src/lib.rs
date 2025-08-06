#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use crate::host::{Result, Result::Err, Result::Ok};
use xrpl_std::core::ledger_objects::current_escrow::get_current_escrow;
use xrpl_std::core::ledger_objects::current_escrow::CurrentEscrow;
use xrpl_std::core::ledger_objects::ledger_object;
use xrpl_std::core::ledger_objects::traits::CurrentEscrowFields;
use xrpl_std::core::types::amount::currency_code::CurrencyCode;
use xrpl_std::core::types::keylets;
use xrpl_std::host;
use xrpl_std::host::trace::{trace, trace_data, trace_num, DataRepr};
use xrpl_std::sfield;

#[unsafe(no_mangle)]
pub fn object_exists(
    keylet_result: Result<keylets::KeyletBytes>,
    keylet_type: &str,
    field: i32,
) -> Result<bool> {
    match keylet_result {
        Ok(keylet) => {
            let _ = trace_data(keylet_type, &keylet, DataRepr::AsHex);

            let slot = unsafe { host::cache_ledger_obj(keylet.as_ptr(), keylet.len(), 0) };
            if slot <= 0 {
                let _ = trace_num("Error: ", slot.into());
                return Err(host::Error::NoFreeSlots);
            }
            if field == 0 {
                let new_field = sfield::PreviousTxnID;
                let _ = trace_num("Getting field: ", new_field.into());
                match ledger_object::get_hash_256_field(slot, new_field) {
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
                match ledger_object::get_account_id_field(slot, field) {
                    Ok(data) => {
                        let _ = trace_data("Field data: ", &data.0, DataRepr::AsHex);
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
    let _ = trace_data("  Account:", &account.0, DataRepr::AsHex);

    let destination = escrow.get_destination().unwrap_or_panic();
    let _ = trace_data("  Destination:", &destination.0, DataRepr::AsHex);

    macro_rules! check_object_exists {
        ($keylet:expr, $type:expr, $field:expr) => {
            match object_exists($keylet, $type, $field) {
                Ok(exists) => {
                    if exists {
                        let _ = trace(concat!(
                            $type,
                            " object exists, proceeding with escrow finish."
                        ));
                    } else {
                        let _ = trace(concat!(
                            $type,
                            " object does not exist, aborting escrow finish."
                        ));
                        return 0;
                    }
                }
                Err(error) => return error.code(),
            }
        };
    }

    let account_keylet = keylets::account_keylet(&account);
    check_object_exists!(account_keylet, "Account", sfield::Account);

    let mut seq = 5;
    let currency_code: &[u8; 3] = b"USD";
    let currency: CurrencyCode = CurrencyCode::from(*currency_code);
    let line_keylet = keylets::line_keylet(&account, &destination, &currency);
    check_object_exists!(line_keylet, "Trustline", sfield::Generic);
    seq += 1;

    let check_keylet = keylets::check_keylet(&account, seq);
    check_object_exists!(check_keylet, "Check", sfield::Account);
    seq += 1;

    let cred_type: &[u8] = b"termsandconditions";
    let credential_keylet = keylets::credential_keylet(&account, &account, cred_type);
    check_object_exists!(credential_keylet, "Credential", sfield::Subject);
    seq += 1;

    let delegate_keylet = keylets::delegate_keylet(&account, &destination);
    check_object_exists!(delegate_keylet, "Delegate", sfield::Account);
    seq += 1;

    let deposit_preauth_keylet = keylets::deposit_preauth_keylet(&account, &destination);
    check_object_exists!(deposit_preauth_keylet, "DepositPreauth", sfield::Account);
    seq += 1;

    let did_keylet = keylets::did_keylet(&account);
    check_object_exists!(did_keylet, "DID", sfield::Account);
    seq += 1;

    let escrow_keylet = keylets::escrow_keylet(&account, seq);
    check_object_exists!(escrow_keylet, "Escrow", sfield::Account);
    seq += 1;

    let nft_offer_keylet = keylets::nft_offer_keylet(&destination, 4);
    check_object_exists!(nft_offer_keylet, "NFTokenOffer", sfield::Owner);

    let offer_keylet = keylets::offer_keylet(&account, seq);
    check_object_exists!(offer_keylet, "Offer", sfield::Account);
    seq += 1;

    let paychan_keylet = keylets::paychan_keylet(&account, &destination, seq);
    check_object_exists!(paychan_keylet, "PayChannel", sfield::Account);
    seq += 1;

    let signers_keylet = keylets::signers_keylet(&account);
    check_object_exists!(signers_keylet, "SignerList", sfield::Generic);
    seq += 1;

    seq += 1; // ticket sequence number is one greater
    let ticket_keylet = keylets::ticket_keylet(&account, seq);
    check_object_exists!(ticket_keylet, "Ticket", sfield::Account);
    // seq += 1;

    1 // All keylets exist, finish the escrow.
}
