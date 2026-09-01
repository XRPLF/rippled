//! Exercises the API that `host_functions!` generates, not the macro itself:
//! the `HostFunctions` trait is implementable and callable both directly and
//! through `&dyn`, and the generated `HostFunctionSpec` and `TraceDataType`
//! tables agree with the declarations in `src/lib.rs`. The macro's own parsing
//! and diagnostics are covered by the unit tests in `xrpl-host-functions-macros`.

use std::cell::RefCell;
use std::collections::HashSet;

use xrpl_host_functions::{
    HASH_LEN, HostError, HostFunctionSpec, HostFunctions, HostResult, TraceDataType, WasmValType,
};

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
    fn check_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
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
    fn escrow_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
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
        _seq: u32,
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
    fn nftoken_offer_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same account-and-sequence shape, for an `Offer`.
    fn offer_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same account-and-scalar shape, for an `Oracle` keyed by document id.
    fn oracle_keylet(&self, account: &[u8], _doc_id: u32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// A two-account-and-sequence shape, for a `PayChannel`; `InvalidAccount` if
    /// either account is empty.
    fn paychannel_keylet(
        &self,
        account: &[u8],
        destination: &[u8],
        _seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        if account.is_empty() || destination.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same account-and-sequence shape, for a `PermissionedDomain`.
    fn permissioned_domain_keylet(
        &self,
        account: &[u8],
        _seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The account-only shape, for a `SignerList`.
    fn signer_list_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same account-and-sequence shape, for a `Ticket`.
    fn ticket_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() {
            return Err(HostError::InvalidAccount);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// The same account-and-sequence shape, for a `Vault`.
    fn vault_keylet(&self, account: &[u8], _seq: u32, out: &mut [u8]) -> HostResult<usize> {
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

    fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()> {
        self.traced
            .borrow_mut()
            .push(format!("{msg}/{data_type:?}/{}", data.len()));
        Ok(())
    }

    /// Reads a data blob and returns the count of bytes stored.
    fn update_data(&self, data: &[u8]) -> HostResult<i32> {
        Ok(data.len() as i32)
    }

    /// Reads an account and an nft id, writes a byte value; `InvalidParams` if either
    /// is empty.
    fn get_nft(&self, account: &[u8], nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if account.is_empty() || nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[account[0]; HASH_LEN])
    }

    /// Reads an nft id, writes a byte value; `InvalidParams` on an empty id.
    fn get_nft_issuer(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[nft_id[0]; HASH_LEN])
    }

    /// The same, for the taxon.
    fn get_nft_taxon(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &nft_id[0].to_le_bytes())
    }

    /// Reads an nft id and returns a scalar; `InvalidParams` on an empty id.
    fn get_nft_flags(&self, nft_id: &[u8]) -> HostResult<i32> {
        if nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        Ok(i32::from(nft_id[0]))
    }

    /// The same, for the transfer fee.
    fn get_nft_transfer_fee(&self, nft_id: &[u8]) -> HostResult<i32> {
        if nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        Ok(i32::from(nft_id[0]))
    }

    /// The same byte-output shape, for the sequence number.
    fn get_nft_sequence(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        if nft_id.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &nft_id[0].to_le_bytes())
    }

    /// A scalar-in float: writes the low byte of `x` as a stand-in float.
    fn float_from_int(&self, x: i64, out: &mut [u8], _mode: i32) -> HostResult<usize> {
        put(out, &[x as u8])
    }

    /// A byte-in float; `InvalidParams` on an empty region.
    fn float_from_uint(&self, x: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// The same, for a serialized amount.
    fn float_from_stamount(&self, amount: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if amount.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[amount[0]])
    }

    /// The same, for a serialized number.
    fn float_from_stnumber(&self, number: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if number.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[number[0]])
    }

    /// A float rounded to an integer, written as bytes.
    fn float_to_int(&self, x: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// Writes a mantissa (its first byte) and an exponent (its first byte) to two
    /// regions, returning their combined length.
    fn float_to_mant_exp(
        &self,
        x: &[u8],
        mantissa_out: &mut [u8],
        exponent_out: &mut [u8],
    ) -> HostResult<usize> {
        if x.is_empty() {
            return Err(HostError::InvalidParams);
        }
        let m = put(mantissa_out, &[x[0]])?;
        let e = put(exponent_out, &[x[0]])?;
        Ok(m + e)
    }

    /// A two-scalar-in float.
    fn float_from_mant_exp(
        &self,
        mantissa: i64,
        _exponent: i32,
        out: &mut [u8],
        _mode: i32,
    ) -> HostResult<usize> {
        put(out, &[mantissa as u8])
    }

    /// Reads two floats and returns a scalar; `InvalidParams` if either is empty.
    fn float_compare(&self, x: &[u8], y: &[u8]) -> HostResult<i32> {
        if x.is_empty() || y.is_empty() {
            return Err(HostError::InvalidParams);
        }
        Ok(i32::from(x[0]) - i32::from(y[0]))
    }

    /// A binary float operator; `InvalidParams` if either operand is empty.
    fn float_add(&self, x: &[u8], y: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() || y.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// The same shape, for subtraction.
    fn float_subtract(&self, x: &[u8], y: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() || y.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// The same shape, for multiplication.
    fn float_multiply(&self, x: &[u8], y: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() || y.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// The same shape, for division.
    fn float_divide(&self, x: &[u8], y: &[u8], out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() || y.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
    }

    /// The same shape, for exponentiation.
    fn float_power(&self, x: &[u8], _n: i32, out: &mut [u8], _mode: i32) -> HostResult<usize> {
        if x.is_empty() {
            return Err(HostError::InvalidParams);
        }
        put(out, &[x[0]])
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
    assert_eq!(host.offer_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.offer_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.oracle_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.oracle_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.paychannel_keylet(&[7; 20], &[8; 20], 5, &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.paychannel_keylet(&[7; 20], &[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(
        host.permissioned_domain_keylet(&[7; 20], 5, &mut out),
        Ok(HASH_LEN)
    );
    assert_eq!(out[0], 7);
    assert_eq!(
        host.permissioned_domain_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.signer_list_keylet(&[7; 20], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.signer_list_keylet(&[], &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.ticket_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.ticket_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.vault_keylet(&[7; 20], 5, &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.vault_keylet(&[], 5, &mut out),
        Err(HostError::InvalidAccount)
    );
    assert_eq!(host.sha512_half(b"abc", &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 3);
    assert_eq!(host.trace("hello", TraceDataType::AsHex, b"xy"), Ok(()));
    assert_eq!(host.update_data(b"abcd"), Ok(4));
    assert_eq!(host.get_nft(&[7; 20], &[9; 32], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 7);
    assert_eq!(
        host.get_nft(&[], &[9; 32], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(host.get_nft_issuer(&[9; 32], &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 9);
    assert_eq!(
        host.get_nft_issuer(&[], &mut out),
        Err(HostError::InvalidParams)
    );
    assert_eq!(host.get_nft_taxon(&[9; 32], &mut out), Ok(1));
    assert_eq!(host.get_nft_flags(&[9; 32]), Ok(9));
    assert_eq!(host.get_nft_flags(&[]), Err(HostError::InvalidParams));
    assert_eq!(host.get_nft_transfer_fee(&[9; 32]), Ok(9));
    assert_eq!(host.get_nft_sequence(&[9; 32], &mut out), Ok(1));
    assert_eq!(host.float_from_int(5, &mut out, 0), Ok(1));
    assert_eq!(host.float_from_uint(&[3; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_from_stamount(&[3; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_from_stnumber(&[3; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_to_int(&[3; 8], &mut out, 0), Ok(1));
    let mut mant = [0u8; 8];
    let mut exp = [0u8; 4];
    assert_eq!(host.float_to_mant_exp(&[3; 8], &mut mant, &mut exp), Ok(2));
    assert_eq!(host.float_from_mant_exp(5, 0, &mut out, 0), Ok(1));
    assert_eq!(host.float_compare(&[9; 8], &[4; 8]), Ok(5));
    assert_eq!(
        host.float_compare(&[], &[4; 8]),
        Err(HostError::InvalidParams)
    );
    assert_eq!(host.float_add(&[3; 8], &[4; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_subtract(&[3; 8], &[4; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_multiply(&[3; 8], &[4; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_divide(&[3; 8], &[4; 8], &mut out, 0), Ok(1));
    assert_eq!(host.float_power(&[3; 8], 2, &mut out, 0), Ok(1));

    assert_eq!(*host.traced.borrow(), ["hello/AsHex/2"]);
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
    assert_eq!(
        host.trace("count", TraceDataType::Int64, &1i64.to_le_bytes()),
        Ok(())
    );

    assert_eq!(*fake.traced.borrow(), ["count/Int64/8"]);
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
            ("offer_id", 350),
            ("oracle_id", 350),
            ("paychan_id", 350),
            ("permissioned_domain_id", 350),
            ("signers_id", 350),
            ("ticket_id", 350),
            ("vault_id", 350),
            ("sha512_half", 2000),
            ("trace", 30),
            ("set_data", 1000),
            ("nft_uri", 5000),
            ("nft_issuer", 70),
            ("nft_taxon", 60),
            ("nft_flags", 60),
            ("nft_xfer_fee", 60),
            ("nft_serial", 60),
            ("float_from_int", 100),
            ("float_from_uint", 130),
            ("float_from_stamount", 150),
            ("float_from_stnumber", 150),
            ("float_to_int", 130),
            ("float_to_mant_exp", 130),
            ("float_from_mant_exp", 100),
            ("float_cmp", 80),
            ("float_add", 160),
            ("float_sub", 160),
            ("float_mult", 300),
            ("float_div", 300),
            ("float_pow", 5500),
        ]
    );
}

/// The wire shape of every function: the parameters a guest's import must declare
/// and the result it must expect, which is what a module fails to instantiate over.
///
/// A change-detector like the table above, and for the same reason — these are
/// consensus input. It is also the one statement of the wasm signature that is *not*
/// derived from the declarations: the literals were read off the `func_wrap` closures
/// `xrpl-wasm-vm` registers, so the two sides of the ABI are compared here rather
/// than one being checked against itself.
///
/// What it catches is arity and value types — the `u32` parameters that read like
/// scalars and are `(ptr, len)` pairs, and `trace`'s missing result. Not order: every
/// region lowers to `i32`, so swapping two parameters leaves the signature identical.
#[test]
fn the_wasm_signatures_match_the_declarations() {
    let table: Vec<String> = HostFunctionSpec::ALL
        .iter()
        .map(|function| format!("{} {}", function.wasm_name(), signature(*function)))
        .collect();

    assert_eq!(
        table,
        [
            "ldgr_index (i32, i32) -> i32",
            "parent_ldgr_time (i32, i32) -> i32",
            "parent_ldgr_hash (i32, i32) -> i32",
            "base_fee (i32, i32) -> i32",
            "amendment_enabled (i32, i32) -> i32",
            "cache_le (i32, i32, i32) -> i32",
            "tx_field (i32, i32, i32) -> i32",
            "home_le_field (i32, i32, i32) -> i32",
            "le_field (i32, i32, i32, i32) -> i32",
            "tx_inner (i32, i32, i32, i32) -> i32",
            "home_le_inner (i32, i32, i32, i32) -> i32",
            "le_inner (i32, i32, i32, i32, i32) -> i32",
            "tx_arr_len (i32) -> i32",
            "home_le_arr_len (i32) -> i32",
            "le_arr_len (i32, i32) -> i32",
            "tx_inner_arr_len (i32, i32) -> i32",
            "home_le_inner_arr_len (i32, i32) -> i32",
            "le_inner_arr_len (i32, i32, i32) -> i32",
            "check_sig (i32, i32, i32, i32, i32, i32) -> i32",
            "accountroot_id (i32, i32, i32, i32) -> i32",
            "amm_id (i32, i32, i32, i32, i32, i32) -> i32",
            "check_id (i32, i32, i32, i32, i32, i32) -> i32",
            "credential_id (i32, i32, i32, i32, i32, i32, i32, i32) -> i32",
            "delegate_id (i32, i32, i32, i32, i32, i32) -> i32",
            "deposit_preauth_id (i32, i32, i32, i32, i32, i32) -> i32",
            "did_id (i32, i32, i32, i32) -> i32",
            "escrow_id (i32, i32, i32, i32, i32, i32) -> i32",
            "trustline_id (i32, i32, i32, i32, i32, i32, i32, i32) -> i32",
            "mpt_issuance_id (i32, i32, i32, i32, i32, i32) -> i32",
            "mptoken_id (i32, i32, i32, i32, i32, i32) -> i32",
            "nft_offer_id (i32, i32, i32, i32, i32, i32) -> i32",
            "offer_id (i32, i32, i32, i32, i32, i32) -> i32",
            "oracle_id (i32, i32, i32, i32, i32, i32) -> i32",
            "paychan_id (i32, i32, i32, i32, i32, i32, i32, i32) -> i32",
            "permissioned_domain_id (i32, i32, i32, i32, i32, i32) -> i32",
            "signers_id (i32, i32, i32, i32) -> i32",
            "ticket_id (i32, i32, i32, i32, i32, i32) -> i32",
            "vault_id (i32, i32, i32, i32, i32, i32) -> i32",
            "sha512_half (i32, i32, i32, i32) -> i32",
            "trace (i32, i32, i32, i32, i32)",
            "set_data (i32, i32) -> i32",
            "nft_uri (i32, i32, i32, i32, i32, i32) -> i32",
            "nft_issuer (i32, i32, i32, i32) -> i32",
            "nft_taxon (i32, i32, i32, i32) -> i32",
            "nft_flags (i32, i32) -> i32",
            "nft_xfer_fee (i32, i32) -> i32",
            "nft_serial (i32, i32, i32, i32) -> i32",
            "float_from_int (i64, i32, i32, i32) -> i32",
            "float_from_uint (i32, i32, i32, i32, i32) -> i32",
            "float_from_stamount (i32, i32, i32, i32, i32) -> i32",
            "float_from_stnumber (i32, i32, i32, i32, i32) -> i32",
            "float_to_int (i32, i32, i32, i32, i32) -> i32",
            "float_to_mant_exp (i32, i32, i32, i32, i32, i32) -> i32",
            "float_from_mant_exp (i64, i32, i32, i32, i32) -> i32",
            "float_cmp (i32, i32, i32, i32) -> i32",
            "float_add (i32, i32, i32, i32, i32, i32, i32) -> i32",
            "float_sub (i32, i32, i32, i32, i32, i32, i32) -> i32",
            "float_mult (i32, i32, i32, i32, i32, i32, i32) -> i32",
            "float_div (i32, i32, i32, i32, i32, i32, i32) -> i32",
            "float_pow (i32, i32, i32, i32, i32, i32) -> i32",
        ]
    );
}

/// `(i32, i32) -> i32`: one function type, spelled as wasm's text format spells the
/// types and as wasmi's own errors report them.
fn signature(function: HostFunctionSpec) -> String {
    let params: Vec<&str> = function
        .wasm_params()
        .iter()
        .copied()
        .map(spelled)
        .collect();

    match function.wasm_result() {
        Some(result) => format!("({}) -> {}", params.join(", "), spelled(result)),
        None => format!("({})", params.join(", ")),
    }
}

fn spelled(val_type: WasmValType) -> &'static str {
    match val_type {
        WasmValType::I32 => "i32",
        WasmValType::I64 => "i64",
    }
}

/// The other half of the wire vocabulary, and the same change-detector argument: the
/// codes are what a guest passes, so they are pinned as literals here. `ALL` is in code
/// order, so the round trip pins the discriminants and not just the membership.
#[test]
fn every_trace_data_type_survives_the_wire() {
    let codes: Vec<i32> = TraceDataType::ALL.iter().map(|t| t.code()).collect();

    assert_eq!(codes, [1, 2, 3, 4, 5, 6, 7]);
    for &data_type in TraceDataType::ALL {
        assert_eq!(TraceDataType::from_code(data_type.code()), Some(data_type));
    }
}

/// A code no declaration names is refused rather than read as a neighbouring type.
/// Zero is the one worth naming: it is what a guest sends by omission.
#[test]
fn an_unnamed_trace_data_type_code_is_refused() {
    for code in [0, -1, 8, i32::MAX, i32::MIN] {
        assert_eq!(TraceDataType::from_code(code), None, "code {code}");
    }
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

/// Every accessor is `const`, so an engine can build its import, signature and gas
/// tables at compile time rather than on every invocation. The assertions sit in
/// `const` blocks so they are checked while compiling, which is the claim; the values
/// themselves are pinned above.
#[test]
fn the_table_is_usable_in_const_context() {
    const NAME: &str = HostFunctionSpec::Trace.wasm_name();
    const GAS: u64 = HostFunctionSpec::Trace.gas();
    const PARAMS: &[WasmValType] = HostFunctionSpec::Trace.wasm_params();
    const RESULT: Option<WasmValType> = HostFunctionSpec::Trace.wasm_result();

    const { assert!(!NAME.is_empty()) };
    const { assert!(GAS > 0) };
    const { assert!(!PARAMS.is_empty()) };
    const { assert!(RESULT.is_none()) };
}
