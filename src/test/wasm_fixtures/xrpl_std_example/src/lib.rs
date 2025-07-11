#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use crate::host::{Result::Err, Result::Ok};
use xrpl_std::core::amount::Amount;
use xrpl_std::core::constants::{ACCOUNT_ONE, ACCOUNT_ZERO};
use xrpl_std::core::current_tx::escrow_finish::{EscrowFinish, get_current_escrow_finish};
use xrpl_std::core::current_tx::traits::{EscrowFinishFields, TransactionCommonFields};
use xrpl_std::core::ledger_objects::current_escrow::{CurrentEscrow, get_current_escrow};
use xrpl_std::core::ledger_objects::traits::CurrentEscrowFields;
use xrpl_std::core::locator::Locator;
use xrpl_std::core::types::account_id::AccountID;
use xrpl_std::core::types::blob::Blob;
use xrpl_std::core::types::hash_256::Hash256;
use xrpl_std::core::types::transaction_type::TransactionType;
use xrpl_std::host;
use xrpl_std::host::trace::{DataRepr, trace, trace_data, trace_num};
use xrpl_std::sfield;

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> bool {
    let _ = trace("$$$$$ STARTING WASM EXECUTION $$$$$");

    // First check account and balance
    {
        // TODO: Peng to fix.
        // let account: AccountID = current_transaction::get_account();
        // let balance = match get_account_balance(&account_id_tx) {
        //     Some(v) => v,
        //     None => return -5,
        // };
        // let _ = trace_num("  Balance of Account Finishing the Escrow:", balance as i64);
        // if balance <= 0 {
        //     // TODO: Use real error codes!
        //     return -9;
        // }
    }

    // ########################################
    // Step #1: Access & Emit All EscrowFinish Fields
    // ########################################

    let escrow_finish: EscrowFinish = get_current_escrow_finish();

    let _ = trace("Step #1: Trace EscrowFinish");
    let _ = trace("{ ");
    let _ = trace("  -- Common Fields");

    // Trace Field: TransactionID
    let current_tx_id: Hash256 = escrow_finish.get_id().unwrap_or_panic();
    let _ = trace_data("  EscrowFinish TxId:", &current_tx_id.0, DataRepr::AsHex);

    // Trace Field: Account
    let account = escrow_finish.get_account().unwrap_or_panic();
    let _ = trace_data("  Account:", &account.0, DataRepr::AsHex);
    if account.0.eq(&ACCOUNT_ONE.0) {
        let _ = trace("    AccountID == ACCOUNT_ONE => TRUE");
    } else {
        let _ = trace("    AccountID == ACCOUNT_ONE => FALSE");
        assert_eq!(account, ACCOUNT_ONE);
    }

    // Trace Field: TransactionType
    let transaction_type: TransactionType = escrow_finish.get_transaction_type().unwrap_or_panic();
    let tx_type_bytes: [u8; 2] = transaction_type.into();
    let _ = trace_data(
        "  TransactionType (EscrowFinish):",
        &tx_type_bytes,
        DataRepr::AsHex,
    );

    // Trace Field: ComputationAllowance
    let computation_allowance: u32 = escrow_finish.get_computation_allowance().unwrap_or_panic();
    let _ = trace_num("  ComputationAllowance:", computation_allowance as i64);

    // Trace Field: Fee
    let fee: Amount = escrow_finish.get_fee().unwrap_or_panic();
    let Amount::Xrp(fee_as_xrp_amount) = fee;
    let _ = trace_num("  Fee:", fee_as_xrp_amount.0 as i64);

    // Trace Field: Sequence
    let sequence: u32 = escrow_finish.get_sequence().unwrap_or_panic();
    let _ = trace_num("  Sequence:", sequence as i64);

    // Trace Field: AccountTxnID
    match escrow_finish.get_account_txn_id() {
        Ok(opt_txn_id) => {
            if let Some(txn_id) = opt_txn_id {
                let _ = trace_data("  AccountTxnID:", &txn_id.0, DataRepr::AsHex);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting AccountTxnID. error_code = ",
                error.code() as i64,
            );
        }
    };

    // Trace Field: Flags
    match escrow_finish.get_flags() {
        Ok(opt_flags) => {
            if let Some(flags) = opt_flags {
                let _ = trace_num("  Flags:", flags as i64);
            }
        }
        Err(error) => {
            let _ = trace_num("  Error getting Flags. error_code = ", error.code() as i64);
        }
    };

    // Trace Field: LastLedgerSequence
    match escrow_finish.get_last_ledger_sequence() {
        Ok(opt_last_ledger_sequence) => {
            if let Some(last_ledger_sequence) = opt_last_ledger_sequence {
                let _ = trace_num("  LastLedgerSequence:", last_ledger_sequence as i64);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting LastLedgerSequence. error_code = ",
                error.code() as i64,
            );
        }
    };

    // Trace Field: NetworkID
    match escrow_finish.get_network_id() {
        Ok(opt_network_id) => {
            if let Some(network_id) = opt_network_id {
                let _ = trace_num("  NetworkID:", network_id as i64);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting NetworkID. error_code = ",
                error.code() as i64,
            );
        }
    };

    // Trace Field: SourceTag
    match escrow_finish.get_source_tag() {
        Ok(opt_source_tag) => {
            if let Some(source_tag) = opt_source_tag {
                let _ = trace_num("  SourceTag:", source_tag as i64);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting SourceTag. error_code = ",
                error.code() as i64,
            );
        }
    };

    // Trace Field: SigningPubKey
    match escrow_finish.get_signing_pub_key() {
        Ok(signing_pub_key) => {
            let _ = trace_data("  SigningPubKey:", &signing_pub_key.0, DataRepr::AsHex);
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting SigningPubKey. error_code = ",
                error.code() as i64,
            );
        }
    };

    // Trace Field: TicketSequence
    match escrow_finish.get_ticket_sequence() {
        Ok(opt_ticket_sequence) => {
            if let Some(ticket_sequence) = opt_ticket_sequence {
                let _ = trace_num("  TicketSequence:", ticket_sequence as i64);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting TicketSequence. error_code = ",
                error.code() as i64,
            );
        }
    };

    let array_len = unsafe { host::get_tx_array_len(sfield::Memos) };
    let _ = trace_num("  Memos array len:", array_len as i64);

    let mut memo_buf = [0u8; 1024];
    let mut locator = Locator::new();
    locator.pack(sfield::Memos);
    locator.pack(0);
    locator.pack(sfield::Memo);
    locator.pack(sfield::MemoType);
    let output_len = unsafe {
        host::get_tx_nested_field(
            locator.get_addr(),
            locator.num_packed_bytes(),
            memo_buf.as_mut_ptr(),
            memo_buf.len(),
        )
    };
    let _ = trace("    Memo #: 1");
    let _ = trace_data(
        "      MemoType:",
        &memo_buf[..output_len as usize],
        DataRepr::AsHex,
    );

    locator.repack_last(sfield::MemoData);
    let output_len = unsafe {
        host::get_tx_nested_field(
            locator.get_addr(),
            locator.num_packed_bytes(),
            memo_buf.as_mut_ptr(),
            memo_buf.len(),
        )
    };
    let _ = trace_data(
        "      MemoData:",
        &memo_buf[..output_len as usize],
        DataRepr::AsHex,
    );

    locator.repack_last(sfield::MemoFormat);
    let output_len = unsafe {
        host::get_tx_nested_field(
            locator.get_addr(),
            locator.num_packed_bytes(),
            memo_buf.as_mut_ptr(),
            memo_buf.len(),
        )
    };
    let _ = trace_data(
        "      MemoFormat:",
        &memo_buf[..output_len as usize],
        DataRepr::AsHex,
    );

    let array_len = unsafe { host::get_tx_array_len(sfield::Signers) };
    let _ = trace_num("  Signers array len:", array_len as i64);

    for i in 0..array_len {
        let mut buf = [0x00; 64];
        let mut locator = Locator::new();
        locator.pack(sfield::Signers);
        locator.pack(i);
        locator.pack(sfield::Account);
        let output_len = unsafe {
            host::get_tx_nested_field(
                locator.get_addr(),
                locator.num_packed_bytes(),
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        if output_len < 0 {
            //TODO rebase on to devnet3, there is an error code commit
            let _ = trace_num("  cannot get Account, error:", output_len as i64);
            break;
        }
        let _ = trace_num("    Signer #:", i as i64);
        let _ = trace_data(
            "     Account:",
            &buf[..output_len as usize],
            DataRepr::AsHex,
        );

        locator.repack_last(sfield::TxnSignature);
        let output_len = unsafe {
            host::get_tx_nested_field(
                locator.get_addr(),
                locator.num_packed_bytes(),
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        if output_len < 0 {
            let _ = trace_num("  cannot get TxnSignature, error:", output_len as i64);
            break;
        }
        let _ = trace_data(
            "     TxnSignature:",
            &buf[..output_len as usize],
            DataRepr::AsHex,
        );

        locator.repack_last(sfield::SigningPubKey);
        let output_len = unsafe {
            host::get_tx_nested_field(
                locator.get_addr(),
                locator.num_packed_bytes(),
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        if output_len < 0 {
            let _ = trace_num(
                "  Error getting SigningPubKey. error_code = ",
                output_len as i64,
            );
            break;
        }
        let _ = trace_data(
            "     SigningPubKey:",
            &buf[..output_len as usize],
            DataRepr::AsHex,
        );
    }

    let txn_signature: Blob = escrow_finish.get_txn_signature().unwrap_or_panic();
    let _ = trace_data(
        "  TxnSignature:",
        &txn_signature.data[..txn_signature.len],
        DataRepr::AsHex,
    );

    let _ = trace("  -- EscrowFinish Fields");

    // Trace Field: Account
    let owner: AccountID = escrow_finish.get_owner().unwrap_or_panic();
    let _ = trace_data("  Owner:", &owner.0, DataRepr::AsHex);
    if owner.0[0].eq(&ACCOUNT_ZERO.0[0]) {
        let _ = trace("    AccountID == ACCOUNT_ZERO => TRUE");
    } else {
        let _ = trace("    AccountID == ACCOUNT_ZERO => FALSE");
        assert_eq!(owner, ACCOUNT_ZERO);
    }

    // Trace Field: OfferSequence
    let offer_sequence: u32 = escrow_finish.get_offer_sequence().unwrap_or_panic();
    let _ = trace_num("  OfferSequence:", offer_sequence as i64);

    // Trace Field: Condition
    match escrow_finish.get_condition() {
        Ok(opt_condition) => {
            if let Some(condition) = opt_condition {
                let _ = trace_data("  Condition:", &condition.0, DataRepr::AsHex);
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting Condition. error_code = ",
                error.code() as i64,
            );
        }
    };

    match escrow_finish.get_fulfillment() {
        Ok(opt_fulfillment) => {
            if let Some(fulfillment) = opt_fulfillment {
                let _ = trace_data(
                    "  Fulfillment:",
                    &fulfillment.data[..fulfillment.len],
                    DataRepr::AsHex,
                );
            }
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting Fulfillment. error_code = ",
                error.code() as i64,
            );
        }
    };

    // CredentialIDs (Array of Hashes)
    let array_len = unsafe { host::get_tx_array_len(sfield::CredentialIDs) };
    let _ = trace_num("  CredentialIDs array len:", array_len as i64);
    for i in 0..array_len {
        let mut buf = [0x00; 32];
        let mut locator = Locator::new();
        locator.pack(sfield::CredentialIDs);
        locator.pack(i);
        let output_len = unsafe {
            host::get_tx_nested_field(
                locator.get_addr(),
                locator.num_packed_bytes(),
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        let _ = trace_data(
            "  CredentialID:",
            &buf[..output_len as usize],
            DataRepr::AsHex,
        );
    }

    let _ = trace("{ ");

    // ########################################
    // Step #2: Access & Emit All Current Escrow ledger object fields
    // ########################################
    let _ = trace("Step #2: Trace Current Escrow Ledger Object");
    let _ = trace("{ ");
    let _ = trace("  -- Common Fields");

    let current_escrow: CurrentEscrow = get_current_escrow();
    match current_escrow.get_account() {
        Ok(account) => {
            let _ = trace_data("  Escrow Account:", &account.0, DataRepr::AsHex);
        }
        Err(error) => {
            let _ = trace_num(
                "  Error getting Account. error_code = ",
                error.code() as i64,
            );
        }
    };

    let _ = trace("  -- Specific Fields");
    let _ = trace("    -- TODO: Finish tracing all fields");
    // TODO: Trace all fields from the Escrow object, including common field.
    // (see https://xrpl.org/docs/references/protocol/ledger-data/ledger-entry-types/escrow)
    let _ = trace("}");

    // ########################################
    // Step #3 [Arbitrary Ledger Object]: Get arbitrary fields from an AccountRoot object.
    // ########################################
    let _ = trace("Step #3: Trace AccountRoot Ledger Object");
    let _ = trace("{ ");
    let _ = trace("  -- Common Fields");
    let _ = trace("    -- TODO: Finish tracing all fields");
    let _ = trace("  -- Specific Fields");
    let _ = trace("    -- TODO: Finish tracing all fields");
    let _ = trace("}");
    // TODO: Implement these.
    // let sender = get_tx_account_id();
    // let dest_balance = get_account_balance(&dest);
    // let escrow_data = get_current_escrow_data();
    // let ed_str = String::from_utf8(escrow_data.clone()).unwrap();
    // let threshold_balance = ed_str.parse::<u64>().unwrap();
    // let pl_time = host::getParentLedgerTime();
    // let e_time = get_current_current_transaction_after();

    // ########################################
    // Step #4 [NFT]: Trace all fields from an NFT
    // ########################################
    let _ = trace("Step #4: Trace Nft Ledger Object");
    let _ = trace("{ ");
    let _ = trace("  -- Common Fields");
    let _ = trace("    -- TODO: Finish tracing all fields");
    let _ = trace("  -- Specific Fields");
    let _ = trace("    -- TODO: Finish tracing all fields");
    let _ = trace("}");

    // ########################################
    // Step #5 [Ledger Headers]: Emit all ledger headers.
    // ########################################
    let _ = trace("Step #5: Trace Ledger Headers");
    let _ = trace("{ ");
    // TODO: Implement this.
    let _ = trace("    -- TODO: Finish tracing all fields");
    let _ = trace("}");

    let _ = trace("$$$$$ WASM EXECUTION COMPLETE $$$$$");

    // TODO: Remove these examples once the above TODOs are completed.
    // Keep the commented out validation code from main branch
    {
        // let mut ledger_sqn = 0i32;
        // if unsafe { xrpl_std::host_lib::get_ledger_sqn((&mut ledger_sqn) as *mut i32 as *mut u8, 4) }
        //     <= 0
        // {
        //     return -10;
        // }
    }
    {
        // let keylet = [
        //     52, 47, 158, 13, 36, 46, 219, 67, 160, 251, 252, 103, 43, 48, 44, 200, 187, 144, 73,
        //     147, 23, 46, 87, 251, 255, 76, 93, 74, 30, 184, 90, 185,
        // ];
        // println!("wasm finish keylet {:?}", keylet);
        //
        // let slot = unsafe { host_lib::cache_ledger_obj(keylet.as_ptr(), keylet.len(), 0) };
        //
        // println!("wasm finish slot {:?}", slot);
        //
        // let mut locator = Locator::new();
        // locator.pack(SignerEntries);
        // let array_len = unsafe {
        //     host_lib::get_ledger_obj_nested_array_len(slot, locator.get_addr(), locator.num_packed_bytes())
        // };
        // println!("wasm finish array_len {:?}", array_len);
        //
        // locator.pack(0);
        // locator.pack(SignerEntry);
        // locator.pack(SignerWeight);
        //
        // let mut weight = 0i32;
        // let nfr = unsafe {
        //     host_lib::get_ledger_obj_nested_field(
        //         slot, locator.get_addr(), locator.num_packed_bytes(),
        //         (&mut weight) as *mut i32 as *mut u8, 4
        //     )
        // };
        //
        // println!("wasm finish get_ledger_obj_nested_field {:?} {}", nfr, weight);
    }
    {
        // let nft_id = [
        //     0, 8, 39, 16, 104, 7, 191, 132, 143, 172, 217, 114, 242, 246, 23, 226, 112, 3, 215, 91,
        //     44, 170, 201, 129, 108, 238, 20, 132, 5, 33, 209, 233,
        // ];
        // let owner = get_tx_account_id().unwrap();
        // if owner.len() != 20 {
        //     return -21;
        // }
        // let mut arr = [0u8; 256];
        // let res = unsafe {
        //     host_lib::get_NFT(
        //         owner.as_ptr(),
        //         owner.len(),
        //         nft_id.as_ptr(),
        //         nft_id.len(),
        //         arr.as_mut_ptr(),
        //         arr.len(),
        //     )
        // };
        //
        // if res != 106 {
        //     return -22;
        // }
    }

    false // <-- If we get here, don't finish the escrow.
}
