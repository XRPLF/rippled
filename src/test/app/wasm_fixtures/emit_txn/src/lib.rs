#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::core::current_tx::contract_call::{ContractCall, get_current_contract_call};
use xrpl_wasm_std::core::current_tx::traits::TransactionCommonFields;
use xrpl_wasm_std::core::submit::inner_objects::build_memo;
use xrpl_wasm_std::core::transaction_types::TT_PAYMENT;
use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::host::{add_txn_field, build_txn, emit_built_txn};
use xrpl_wasm_std::sfield;

// ============================================================================
// Constants
// ============================================================================

/// Custom error code for transaction failures
const CUSTOM_ERROR_CODE: i32 = -18;

/// XRPL encoding markers
mod markers {
    pub const ARRAY_END: u8 = 0xF1;
    pub const OBJECT_END: u8 = 0xE1;
}

/// Buffer sizes
mod buffer_sizes {
    pub const MEMO_BUFFER: usize = 256;
    pub const MEMOS_ARRAY: usize = 1024;
    pub const DESTINATION: usize = 21;
}

// ============================================================================
// Helper Functions
// ============================================================================

/// Builds a complete memos array from individual memo buffers
///
/// # Arguments
/// * `buffer` - Output buffer for the complete memos array
/// * `memo_buffers` - Slice of memo data and their lengths
///
/// # Returns
/// Total length of the memos array including the end marker
fn build_memos_array(
    buffer: &mut [u8; buffer_sizes::MEMOS_ARRAY],
    memo_buffers: &[(&[u8], usize)]
) -> usize {
    let mut position = 0;

    // Copy each memo into the array
    for (memo_data, memo_length) in memo_buffers {
        buffer[position..position + memo_length].copy_from_slice(&memo_data[..*memo_length]);
        position += memo_length;
    }

    // Terminate the array
    buffer[position] = markers::ARRAY_END;
    position + 1
}

/// Adds the amount field to the transaction
///
/// # Arguments
/// * `txn_index` - Transaction builder index
/// * `amount_drops` - Amount in drops (192 in this example)
///
/// # Returns
/// Result code from add_txn_field
unsafe fn add_amount_field(txn_index: i32) -> i32 {
    // 192 drops encoded as XRPL Amount
    const AMOUNT_BYTES: [u8; 8] = [
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0
    ];

    add_txn_field(
        txn_index,
        sfield::Amount,
        AMOUNT_BYTES.as_ptr(),
        AMOUNT_BYTES.len()
    )
}

/// Adds the destination field to the transaction
///
/// # Arguments
/// * `txn_index` - Transaction builder index
/// * `destination` - Destination account ID
///
/// # Returns
/// Result code from add_txn_field
unsafe fn add_destination_field(txn_index: i32, destination: &AccountID) -> i32 {
    let mut dest_buffer = [0u8; buffer_sizes::DESTINATION];
    dest_buffer[0] = 0x14; // Length prefix for 20-byte account
    dest_buffer[1..21].copy_from_slice(&destination.0);

    add_txn_field(
        txn_index,
        sfield::Destination,
        dest_buffer.as_ptr(),
        dest_buffer.len()
    )
}

/// Adds the memos field to the transaction
///
/// # Arguments
/// * `txn_index` - Transaction builder index
///
/// # Returns
/// Result code from add_txn_field
unsafe fn add_memos_field(txn_index: i32) -> i32 {
    use core::mem::MaybeUninit;

    // Uninitialized backing buffer (no zeroing => no memory.fill)
    let mut memos_uninit: MaybeUninit<[u8; buffer_sizes::MEMOS_ARRAY]> = MaybeUninit::uninit();
    let base = memos_uninit.as_mut_ptr() as *mut u8;

    // Helper: get a 256-byte window at current position
    #[inline(always)]
    unsafe fn at<'a>(base: *mut u8, pos: usize) -> &'a mut [u8; buffer_sizes::MEMO_BUFFER] {
        &mut *(base.add(pos) as *mut [u8; buffer_sizes::MEMO_BUFFER])
    }

    let mut pos = 0usize;

    // Write each Memo directly into the big buffer
    let len1 = build_memo(
        at(base, pos),
        Some(b"invoice"),
        Some(b"INV-2024-001"),
        Some(b"text/plain")
    );
    pos += len1;

    let len2 = build_memo(
        at(base, pos),
        Some(b"note"),
        Some(b"Payment for consulting services"),
        Some(b"text/plain")
    );
    pos += len2;

    let len3 = build_memo(
        at(base, pos),
        None,
        Some(b"Additional reference: Project Alpha"),
        None
    );
    pos += len3;

    // Terminate the array
    *base.add(pos) = markers::ARRAY_END;
    pos += 1;

    add_txn_field(
        txn_index,
        sfield::Memos,
        base,
        pos
    )
}

// ============================================================================
// Main Entry Point
// ============================================================================

/// Main hook function that builds and emits a payment transaction with memos
///
/// This function:
/// 1. Retrieves the current contract call context
/// 2. Builds a payment transaction
/// 3. Adds amount, destination, and memos fields
/// 4. Emits the completed transaction
///
/// # Returns
/// - 0 on success
/// - Negative error code on failure
#[unsafe(no_mangle)]
pub extern "C" fn emit() -> i32 {
    // Get contract context
    let contract_call: ContractCall = get_current_contract_call();
    let account = contract_call.get_account().unwrap();

    // Initialize payment transaction
    let txn_index = 0;
    let build_result = unsafe { build_txn(TT_PAYMENT) };
    if build_result < 0 {
        return CUSTOM_ERROR_CODE;
    }

    // Build transaction fields
    unsafe {
        // Add amount field
        if add_amount_field(txn_index) < 0 {
            return CUSTOM_ERROR_CODE;
        }

        // Add destination field
        if add_destination_field(txn_index, &account) < 0 {
            return CUSTOM_ERROR_CODE;
        }

        // Add memos field
        if add_memos_field(txn_index) < 0 {
            return CUSTOM_ERROR_CODE;
        }

        // Emit the completed transaction
        let emission_result = emit_built_txn(txn_index);
        if emission_result < 0 {
            return emission_result;
        }
    }

    0 // Success
}
