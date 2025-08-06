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

    let account_keylet = keylets::account_keylet(&account);
    match object_exists(account_keylet, "Account", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Check object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Check object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };

    let mut seq = 5;
    let currency_code: &[u8; 3] = b"USD";
    let currency: CurrencyCode = CurrencyCode::from(*currency_code);
    let line_keylet = keylets::line_keylet(&account, &destination, &currency);
    match object_exists(line_keylet, "Trustline", sfield::Generic) {
        Ok(exists) => {
            if exists {
                let _ = trace("Trustline object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Trustline object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let check_keylet = keylets::check_keylet(&account, seq);
    match object_exists(check_keylet, "Check", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Check object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Check object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let cred_type: &[u8] = b"termsandconditions";
    let credential_keylet = keylets::credential_keylet(&account, &account, cred_type);
    match object_exists(credential_keylet, "Credential", sfield::Subject) {
        Ok(exists) => {
            if exists {
                let _ = trace("Credential object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Credential object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let delegate_keylet = keylets::delegate_keylet(&account, &destination);
    match object_exists(delegate_keylet, "Delegate", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Delegate object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Delegate object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let deposit_preauth_keylet = keylets::deposit_preauth_keylet(&account, &destination);
    match object_exists(deposit_preauth_keylet, "DepositPreauth", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("DepositPreauth object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("DepositPreauth object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let did_keylet = keylets::did_keylet(&account);
    match object_exists(did_keylet, "DID", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("DID object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("DID object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let escrow_keylet = keylets::escrow_keylet(&account, seq);
    match object_exists(escrow_keylet, "Escrow", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Escrow object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Escrow object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let nft_offer_keylet = keylets::nft_offer_keylet(&destination, 4);
    match object_exists(nft_offer_keylet, "NFTokenOffer", sfield::Owner) {
        Ok(exists) => {
            if exists {
                let _ = trace("NFTokenOffer object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("NFTokenOffer object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };

    let offer_keylet = keylets::offer_keylet(&account, seq);
    match object_exists(offer_keylet, "Offer", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Offer object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Offer object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let paychan_keylet = keylets::paychan_keylet(&account, &destination, seq);
    match object_exists(paychan_keylet, "PayChannel", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("PayChannel object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("PayChannel object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    let signers_keylet = keylets::signers_keylet(&account);
    match object_exists(signers_keylet, "SignerList", sfield::Generic) {
        Ok(exists) => {
            if exists {
                let _ = trace("SignerList object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("SignerList object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    seq += 1;

    seq += 1; // ticket sequence number is one greater
    let ticket_keylet = keylets::ticket_keylet(&account, seq);
    match object_exists(ticket_keylet, "Ticket", sfield::Account) {
        Ok(exists) => {
            if exists {
                let _ = trace("Ticket object exists, proceeding with escrow finish.");
            } else {
                let _ = trace("Ticket object does not exist, aborting escrow finish.");
                return 0;
            }
        }
        Err(error) => return error.code(),
    };
    // seq += 1;

    1 // All keylets exist, finish the escrow.
}
