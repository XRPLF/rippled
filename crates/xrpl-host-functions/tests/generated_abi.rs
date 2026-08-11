//! Exercises what `host_functions!` generates: the trait is implementable and
//! the spec table agrees with the declarations in `src/lib.rs`.

use std::cell::RefCell;
use std::collections::HashSet;

use xrpl_host_functions::{HASH_LEN, HostError, HostFunctionSpec, HostFunctions, HostResult};

/// Records what it was asked to do; enough to prove the trait is usable.
///
/// Every method takes `&self`, so a host that records anything keeps it behind
/// interior mutability.
#[derive(Default)]
struct FakeHost {
    traced: RefCell<Vec<String>>,
}

/// The contract every byte-producing host function follows: write only if the
/// value fits, and report its true length either way, so the engine can turn a
/// value that doesn't fit into `BufferTooSmall` without the host knowing the
/// guest's buffer size.
fn put(out: &mut [u8], value: &[u8]) -> HostResult<usize> {
    if let Some(dst) = out.get_mut(..value.len()) {
        dst.copy_from_slice(value);
    }
    Ok(value.len())
}

impl HostFunctions for FakeHost {
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &7u32.to_le_bytes())
    }

    fn get_parent_ledger_time(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &9u32.to_le_bytes())
    }

    fn get_parent_ledger_hash(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &[0xab; HASH_LEN])
    }

    fn get_base_fee(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &10u32.to_le_bytes())
    }

    /// Returns a flag rather than bytes, and reads its input: enabled unless empty.
    fn is_amendment_enabled(&self, amendment: &[u8]) -> HostResult<i32> {
        Ok(i32::from(!amendment.is_empty()))
    }

    /// Returns a slot: the requested one, or slot 1 when asked to pick.
    fn cache_ledger_obj(&self, _obj_id: &[u8], cache_idx: i32) -> HostResult<i32> {
        Ok(if cache_idx == 0 { 1 } else { cache_idx })
    }

    /// A field getter over the transaction; fails on a negative selector.
    fn get_tx_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        if field < 0 {
            return Err(HostError::FieldNotFound);
        }
        put(out, &[field as u8])
    }

    /// Fails on a field it doesn't know, so the error channel is exercised too.
    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        if field < 0 {
            return Err(HostError::FieldNotFound);
        }
        put(out, &[field as u8])
    }

    /// A field getter over a cached object, keyed by slot and selector.
    fn get_ledger_obj_field(
        &self,
        cache_idx: i32,
        field: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        if cache_idx <= 0 || field < 0 {
            return Err(HostError::FieldNotFound);
        }
        put(out, &[cache_idx as u8, field as u8])
    }

    /// A nested-field getter over the transaction, keyed by the locator bytes.
    fn get_tx_nested_field(&self, locator: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        put(out, &[locator[0], locator.len() as u8])
    }

    /// The same, over the current ledger object.
    fn get_current_ledger_obj_nested_field(
        &self,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        put(out, &[locator.len() as u8, locator[0]])
    }

    /// The same, over a cached object keyed by slot.
    fn get_ledger_obj_nested_field(
        &self,
        cache_idx: i32,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if cache_idx <= 0 || locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        put(out, &[cache_idx as u8, locator[0]])
    }

    /// A scalar-in, scalar-out count; `NoArray` on a negative selector.
    fn get_tx_array_len(&self, field: i32) -> HostResult<i32> {
        if field < 0 {
            return Err(HostError::NoArray);
        }
        Ok(field)
    }

    /// The same, over the current ledger object.
    fn get_current_ledger_obj_array_len(&self, field: i32) -> HostResult<i32> {
        if field < 0 {
            return Err(HostError::NoArray);
        }
        Ok(field + 1)
    }

    /// The same, over a cached object keyed by slot.
    fn get_ledger_obj_array_len(&self, cache_idx: i32, field: i32) -> HostResult<i32> {
        if cache_idx <= 0 || field < 0 {
            return Err(HostError::NoArray);
        }
        Ok(cache_idx + field)
    }

    /// A nested array-length getter, keyed by the locator bytes.
    fn get_tx_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        if locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        Ok(locator.len() as i32)
    }

    /// The same, over the current ledger object.
    fn get_current_ledger_obj_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        if locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        Ok(locator.len() as i32 + 1)
    }

    /// The same, over a cached object keyed by slot.
    fn get_ledger_obj_nested_array_len(&self, cache_idx: i32, locator: &[u8]) -> HostResult<i32> {
        if cache_idx <= 0 || locator.is_empty() {
            return Err(HostError::LocatorMalformed);
        }
        Ok(cache_idx + locator.len() as i32)
    }

    /// Reads three regions and returns a verdict: valid unless the signature is empty.
    fn check_signature(
        &self,
        _message: &[u8],
        signature: &[u8],
        _pubkey: &[u8],
    ) -> HostResult<i32> {
        Ok(i32::from(!signature.is_empty()))
    }

    /// A keylet getter: reads an account, writes a 32-byte keylet; `InvalidAccount`
    /// on an empty account.
    fn account_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// A two-asset keylet getter; `InvalidParams` if the two assets are equal.
    fn amm_keylet(&self, asset1: &[u8], asset2: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if asset1 == asset2 {
            return Err(HostError::InvalidParams);
        }
        put(out, &[asset1.len() as u8; HASH_LEN])
    }

    /// A keylet from an account and a sequence; `InvalidAccount` on an empty account.
    fn check_keylet(&self, account: &[u8], _seq: i32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// A keylet from subject, issuer, and credential type; `InvalidAccount` if either
    /// account is empty, `InvalidParams` if the type is empty.
    fn credential_keylet(
        &self,
        subject: &[u8],
        issuer: &[u8],
        credential_type: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if subject.is_empty() || issuer.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        if credential_type.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[subject[0]; HASH_LEN])
    }

    /// A keylet from two accounts; `InvalidAccount` if either is empty, `InvalidParams`
    /// if they are equal.
    fn delegate_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if account.is_empty() || authorize.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        if account == authorize {
            return Err(HostError::InvalidParams);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same two-account shape, for a `DepositPreauth`.
    fn deposit_preauth_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if account.is_empty() || authorize.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        if account == authorize {
            return Err(HostError::InvalidParams);
        }
        put(out, &[authorize[0]; HASH_LEN])
    }

    /// A single-account keylet, for a `DID`.
    fn did_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The account-and-sequence shape, for an `Escrow`.
    fn escrow_keylet(&self, account: &[u8], _seq: i32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// A keylet from two accounts and a currency; `InvalidAccount` if either account
    /// is empty, `InvalidParams` if they are equal or the currency is empty.
    fn trust_line_keylet(
        &self,
        account1: &[u8],
        account2: &[u8],
        currency: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        if account1.is_empty() || account2.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        if account1 == account2 || currency.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[account1[0]; HASH_LEN])
    }

    /// The issuer-and-sequence shape, for an `MPTokenIssuance`.
    fn mptoken_issuance_keylet(
        &self,
        issuer: &[u8],
        _seq: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        if issuer.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[issuer[0]; HASH_LEN])
    }

    /// A keylet from an MPT id and a holder; `InvalidParams` if the id is empty,
    /// `InvalidAccount` if the holder is empty.
    fn mptoken_keylet(&self, mptid: &[u8], holder: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if mptid.is_empty() {
            return Err(HostError::InvalidParams);
        }
        if holder.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[mptid[0]; HASH_LEN])
    }

    /// The account-and-sequence shape, for an `NFTokenOffer`.
    fn nftoken_offer_keylet(&self, account: &[u8], _seq: i32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize> {
        let mut digest = [0; HASH_LEN];
        digest[0] = data.len() as u8;
        put(out, &digest)
    }

    fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()> {
        self.traced
            .borrow_mut()
            .push(format!("{msg}/{}/{as_hex}", data.len()));
        Ok(())
    }

    fn trace_num(&self, msg: &str, number: i64) -> HostResult<()> {
        self.traced.borrow_mut().push(format!("{msg}={number}"));
        Ok(())
    }
}

#[test]
fn the_trait_is_implementable() {
    let host = FakeHost::default();
    let mut out = [0u8; HASH_LEN];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(out[..4], [7, 0, 0, 0]);
    assert_eq!(host.get_parent_ledger_time(&mut out), Ok(4));
    assert_eq!(out[..4], [9, 0, 0, 0]);
    assert_eq!(host.get_parent_ledger_hash(&mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 0xab);
    assert_eq!(host.get_base_fee(&mut out), Ok(4));
    assert_eq!(out[..4], [10, 0, 0, 0]);
    assert_eq!(host.is_amendment_enabled(&[1; 32]), Ok(1));
    assert_eq!(host.is_amendment_enabled(&[]), Ok(0));
    assert_eq!(host.cache_ledger_obj(&[1; 32], 0), Ok(1));
    assert_eq!(host.cache_ledger_obj(&[1; 32], 5), Ok(5));
    assert_eq!(host.get_tx_field(5, &mut out), Ok(1));
    assert_eq!(out[0], 5);
    assert_eq!(host.get_current_ledger_obj_field(3, &mut out), Ok(1));
    assert_eq!(out[0], 3);
    assert_eq!(host.get_ledger_obj_field(2, 4, &mut out), Ok(2));
    assert_eq!(out[..2], [2, 4]);
    assert_eq!(host.get_tx_nested_field(&[9, 0, 0, 0], &mut out), Ok(2));
    assert_eq!(out[..2], [9, 4]);
    assert_eq!(
        host.get_current_ledger_obj_nested_field(&[9, 0, 0, 0], &mut out),
        Ok(2)
    );
    assert_eq!(out[..2], [4, 9]);
    assert_eq!(
        host.get_ledger_obj_nested_field(3, &[9, 0, 0, 0], &mut out),
        Ok(2)
    );
    assert_eq!(out[..2], [3, 9]);
    assert_eq!(host.get_tx_array_len(3), Ok(3));
    assert_eq!(host.get_tx_array_len(-1), Err(HostError::NoArray));
    assert_eq!(host.get_current_ledger_obj_array_len(3), Ok(4));
    assert_eq!(
        host.get_current_ledger_obj_array_len(-1),
        Err(HostError::NoArray)
    );
    assert_eq!(host.get_ledger_obj_array_len(2, 3), Ok(5));
    assert_eq!(host.get_ledger_obj_array_len(0, 3), Err(HostError::NoArray));
    assert_eq!(host.get_tx_nested_array_len(&[9, 0, 0, 0]), Ok(4));
    assert_eq!(
        host.get_tx_nested_array_len(&[]),
        Err(HostError::LocatorMalformed)
    );
    assert_eq!(
        host.get_current_ledger_obj_nested_array_len(&[9, 0, 0, 0]),
        Ok(5)
    );
    assert_eq!(
        host.get_current_ledger_obj_nested_array_len(&[]),
        Err(HostError::LocatorMalformed)
    );
    assert_eq!(
        host.get_ledger_obj_nested_array_len(2, &[9, 0, 0, 0]),
        Ok(6)
    );
    assert_eq!(
        host.get_ledger_obj_nested_array_len(0, &[9, 0, 0, 0]),
        Err(HostError::LocatorMalformed)
    );
    assert_eq!(host.check_signature(b"msg", b"sig", b"pk"), Ok(1));
    assert_eq!(host.check_signature(b"msg", b"", b"pk"), Ok(0));
    assert_eq!(host.account_keylet(&[7; 20], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.account_keylet(&[], &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.amm_keylet(&[1; 20], &[2; 40], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 20);
    assert_eq!(
        host.amm_keylet(&[1; 20], &[1; 20], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(host.check_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.check_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.credential_keylet(&[7; 20], &[8; 20], b"cred", &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.credential_keylet(&[], &[8; 20], b"cred", &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.delegate_keylet(&[7; 20], &[8; 20], &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.delegate_keylet(&[], &[8; 20], &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.deposit_preauth_keylet(&[7; 20], &[8; 20], &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 8);
    assert_eq!(
        host.deposit_preauth_keylet(&[7; 20], &[7; 20], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(host.did_keylet(&[7; 20], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.did_keylet(&[], &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.escrow_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.escrow_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.trust_line_keylet(&[7; 20], &[8; 20], &[1; 20], &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.trust_line_keylet(&[7; 20], &[7; 20], &[1; 20], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(
        host.mptoken_issuance_keylet(&[7; 20], 5, &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.mptoken_issuance_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.mptoken_keylet(&[9; 24], &[8; 20], &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 9);
    assert_eq!(
        host.mptoken_keylet(&[], &[8; 20], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(
        host.nftoken_offer_keylet(&[7; 20], 5, &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.nftoken_offer_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.sha512_half(b"abc", &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 3);
    assert_eq!(host.trace("hello", b"xy", true), Ok(()));
    assert_eq!(host.trace_num("count", -1), Ok(()));

    assert_eq!(*host.traced.borrow(), ["hello/2/true", "count=-1"]);
}

/// The error channel every declaration carries: an `Err` the VM turns into the
/// wire's negative return code.
#[test]
fn a_failing_call_reports_its_error_code() {
    let host = FakeHost::default();
    let mut out = [0u8; 8];

    assert_eq!(
        host.get_current_ledger_obj_field(-1, &mut out),
        Err(HostError::FieldNotFound)
    );
    assert_eq!(HostError::FieldNotFound.code(), -2);
}

/// A host reports the value's true length even when it cannot write it, which is
/// what lets the engine answer `BufferTooSmall` on the guest's behalf.
#[test]
fn a_short_buffer_still_reports_the_true_length() {
    let host = FakeHost::default();
    let mut out = [0u8; 2];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(
        out,
        [0, 0],
        "nothing is written when the value does not fit"
    );
}

/// The VM reaches the host as one shared trait object held in the wasmi `Store`,
/// which is what the `&self` receivers are for.
#[test]
fn the_trait_is_callable_through_a_shared_trait_object() {
    let fake = FakeHost::default();
    let host: &dyn HostFunctions = &fake;
    let mut out = [0u8; 4];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(host.trace_num("count", 1), Ok(()));

    assert_eq!(*fake.traced.borrow(), ["count=1"]);
}

/// The whole table, written out: the one place the ABI's wire names and gas costs
/// appear as literals, and a deliberate change-detector, since both are consensus
/// input. Everything else reads `HostFunctionSpec::gas()` instead.
///
/// `ALL` is in declaration order, so comparing the whole vec pins the order and the
/// membership too.
#[test]
fn the_spec_table_matches_the_declarations() {
    let table: Vec<(&str, u64)> = HostFunctionSpec::ALL
        .iter()
        .map(|function| (function.wasm_name(), function.gas()))
        .collect();

    assert_eq!(
        table,
        [
            ("ldgr_index", 60),
            ("parent_ldgr_time", 60),
            ("parent_ldgr_hash", 60),
            ("base_fee", 60),
            ("amendment_enabled", 100),
            ("cache_le", 5000),
            ("tx_field", 70),
            ("home_le_field", 70),
            ("le_field", 70),
            ("tx_inner", 110),
            ("home_le_inner", 110),
            ("le_inner", 110),
            ("tx_arr_len", 40),
            ("home_le_arr_len", 40),
            ("le_arr_len", 40),
            ("tx_inner_arr_len", 70),
            ("home_le_inner_arr_len", 70),
            ("le_inner_arr_len", 70),
            ("check_sig", 300),
            ("accountroot_id", 350),
            ("amm_id", 450),
            ("check_id", 350),
            ("credential_id", 350),
            ("delegate_id", 350),
            ("deposit_preauth_id", 350),
            ("did_id", 350),
            ("escrow_id", 350),
            ("trustline_id", 400),
            ("mpt_issuance_id", 350),
            ("mptoken_id", 500),
            ("nft_offer_id", 350),
            ("sha512_half", 2000),
            ("trace", 500),
            ("trace_num", 500),
        ]
    );
}

/// `ALL` is what a wasm engine iterates to register imports, so no two declarations
/// may collapse to the same wire name. The table above pins membership and order;
/// this adds only uniqueness, and restates nothing.
#[test]
fn every_variant_appears_in_all_exactly_once() {
    let names: HashSet<&str> = HostFunctionSpec::ALL
        .iter()
        .map(|function| function.wasm_name())
        .collect();

    assert_eq!(names.len(), HostFunctionSpec::ALL.len());
}

/// Both accessors are `const`, so an engine can build its import and gas tables at
/// compile time rather than on every invocation. The assertions sit in `const`
/// blocks so they are checked while compiling, which is the claim; the values
/// themselves are pinned above.
#[test]
fn the_table_is_usable_in_const_context() {
    const NAME: &str = HostFunctionSpec::Trace.wasm_name();
    const GAS: u64 = HostFunctionSpec::Trace.gas();

    const { assert!(!NAME.is_empty()) };
    const { assert!(GAS > 0) };
}
