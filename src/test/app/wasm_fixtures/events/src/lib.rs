#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::core::event::codec_v2::{
    EventBuffer, event_add_u8, event_add_u16, event_add_u32, event_add_u64,
    event_add_u128, event_add_u160, event_add_u192, event_add_u256, event_add_amount, event_add_account,
    event_add_currency, event_add_str
};

#[unsafe(no_mangle)]
pub extern "C" fn events() -> i32 {
    let mut buf = EventBuffer::new();

    // STI_AMOUNT
    const AMOUNT: [u8; 8] = [
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0
    ];
    if event_add_amount(&mut buf, "amount", &AMOUNT).is_err() {
        return -1;
    }

    // STI_CURRENCY
    const CURRENCY: [u8; 20] = [
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x55, 0x53, 0x44, 0x00,
        0x00, 0x00, 0x00, 0x00
    ];
    if event_add_currency(&mut buf, "currency", &CURRENCY).is_err() {
        return -1;
    }

    // STI_ACCOUNT
    const ACCOUNT: [u8; 20] = [
        0x59, 0x69, 0x15, 0xCF, 0xDE, 0xEE, 0x3A, 0x69,
        0x5B, 0x3E, 0xFD, 0x6B, 0xDA, 0x9A, 0xC7, 0x88,
        0xA3, 0x68, 0xB7, 0xB
    ];
    let account = AccountID(ACCOUNT);
    if event_add_account(&mut buf, "destination", &account.0).is_err() {
        return -1;
    }

    // STI_UINT128
    if event_add_u128(&mut buf, "uint128", &[0u8; 16]).is_err() {
        return -1;
    }

    // STI_UINT16
    if event_add_u16(&mut buf, "uint16", 16).is_err() {
        return -1;
    }

    // STI_UINT160
    if event_add_u160(&mut buf, "uint160", &[0u8; 20]).is_err() {
        return -1;
    }

    // STI_UINT192
    if event_add_u192(&mut buf, "uint192", &[0u8; 24]).is_err() {
        return -1;
    }

    // STI_UINT256
    if event_add_u256(&mut buf, "uint256", &[0u8; 32]).is_err() {
        return -1;
    }

    // STI_UINT32
    if event_add_u32(&mut buf, "uint32", 32).is_err() {
        return -1;
    }

    // STI_UINT64
    if event_add_u64(&mut buf, "uint64", 64).is_err() {
        return -1;
    }

    // STI_UINT8
    if event_add_u8(&mut buf, "uint8", 8).is_err() {
        return -1;
    }

    // STI_VL
    if event_add_str(&mut buf, "vl", "Hello, World!").is_err() {
        return -1;
    }

    // STI_ISSUE (XRP)
    // STI_ISSUE (IOU)
    // STI_ISSUE (MPT)

    if buf.emit("event1").is_err() {
        return -1;
    }
    0
}
