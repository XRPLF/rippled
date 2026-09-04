//! What this engine does with each host call, once the ABI's own machinery has
//! taken the call apart.
//!
//! `wasmi_glue!` expands to the [`HostFunctionBodies`] trait and
//! [`register_host_functions`], both generated from the declarations in
//! `xrpl-host-functions` — so the wasm signature every closure is registered at is
//! the one [`crate::check`] screens an import by. Hand-written here is one body per
//! declaration, and the compiler will not accept the `impl` without all of them.
//! [`glue_env`] is this engine's side of that macro's contract.
//!
//! **A body charges no gas and touches no wire encoding.** The generated closure
//! does both, around the call, so a body says only what the call *is*.
//!
//! Four shapes cover 59 of the 60, each decided by the declaration's own types:
//!
//! - a value the host answers directly — read the arguments, call the host;
//! - [`write_into`], for a value written straight to the guest's output region:
//!   the call reads no guest memory, so the host can be handed a `&mut` view of it;
//! - [`write_buffered`], for one that also reads: the host fills the run's scratch
//!   buffer, and it is copied out once every rule has passed, which is what lets
//!   the inputs stay borrowed rather than copied;
//! - [`write_mant_exp`], for the one call that writes two regions.
//!
//! `trace` is the sixtieth: its declared `HostResult<()>` gives it a
//! `CallResult<()>` body and the `charged_unreported` helper.

use crate::abi::{CallResult, guest_memory, write_buffered, write_into, write_mant_exp};
use crate::args::{InBytes, InStr, InU32, OutBytes, TraceCode};
use crate::vm::VmState;
use wasmi::Caller;

/// Everything `wasmi_glue!` names on this side, gathered where the macro can be
/// handed it — so a rename in `abi.rs` or `args.rs` is an unresolved import here
/// rather than a name resolved against whatever the call site has in scope.
///
/// The shapes are elsewhere and cannot be stated here: `args.rs` implements
/// `FromWasmRegion`/`FromWasmScalar`, and the expansion pins each charging
/// helper's signature itself.
mod glue_env {
    pub(crate) use crate::abi::{CallResult, charged, charged_unreported};
    pub(crate) use crate::args::{InBytes, InStr, InU32, OutBytes, TraceCode};
    pub(crate) use crate::vm::VmState;
}

xrpl_host_functions::wasmi_glue!(glue_env);

/// The bodies this engine registers, named as one type so
/// [`register_host_functions`] can be given them. Never built: every body is an
/// associated function and the host it calls comes from the store.
pub(crate) struct Bodies {}

impl HostFunctionBodies for Bodies {
    fn get_ledger_sqn(caller: &mut Caller<'_, VmState<'_>>, out: OutBytes) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.get_ledger_sqn(out))
    }

    fn get_parent_ledger_time(
        caller: &mut Caller<'_, VmState<'_>>,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.get_parent_ledger_time(out))
    }

    fn get_parent_ledger_hash(
        caller: &mut Caller<'_, VmState<'_>>,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.get_parent_ledger_hash(out))
    }

    fn get_base_fee(caller: &mut Caller<'_, VmState<'_>>, out: OutBytes) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.get_base_fee(out))
    }

    fn is_amendment_enabled(
        caller: &mut Caller<'_, VmState<'_>>,
        amendment: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.is_amendment_enabled(amendment.read(memory)?)?)
    }

    fn cache_ledger_obj(
        caller: &mut Caller<'_, VmState<'_>>,
        obj_id: InBytes,
        cache_idx: i32,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.cache_ledger_obj(obj_id.read(memory)?, cache_idx)?)
    }

    fn get_tx_field(
        caller: &mut Caller<'_, VmState<'_>>,
        field: i32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.get_tx_field(field, out))
    }

    fn get_current_ledger_obj_field(
        caller: &mut Caller<'_, VmState<'_>>,
        field: i32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| {
            host.get_current_ledger_obj_field(field, out)
        })
    }

    fn get_ledger_obj_field(
        caller: &mut Caller<'_, VmState<'_>>,
        cache_idx: i32,
        field: i32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| {
            host.get_ledger_obj_field(cache_idx, field, out)
        })
    }

    fn get_tx_nested_field(
        caller: &mut Caller<'_, VmState<'_>>,
        locator: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_tx_nested_field(locator.read(memory)?, buf)
        })
    }

    fn get_current_ledger_obj_nested_field(
        caller: &mut Caller<'_, VmState<'_>>,
        locator: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_current_ledger_obj_nested_field(locator.read(memory)?, buf)
        })
    }

    fn get_ledger_obj_nested_field(
        caller: &mut Caller<'_, VmState<'_>>,
        cache_idx: i32,
        locator: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_ledger_obj_nested_field(cache_idx, locator.read(memory)?, buf)
        })
    }

    fn get_tx_array_len(caller: &mut Caller<'_, VmState<'_>>, field: i32) -> CallResult<i32> {
        Ok(caller.data().host.get_tx_array_len(field)?)
    }

    fn get_current_ledger_obj_array_len(
        caller: &mut Caller<'_, VmState<'_>>,
        field: i32,
    ) -> CallResult<i32> {
        Ok(caller.data().host.get_current_ledger_obj_array_len(field)?)
    }

    fn get_ledger_obj_array_len(
        caller: &mut Caller<'_, VmState<'_>>,
        cache_idx: i32,
        field: i32,
    ) -> CallResult<i32> {
        Ok(caller
            .data()
            .host
            .get_ledger_obj_array_len(cache_idx, field)?)
    }

    fn get_tx_nested_array_len(
        caller: &mut Caller<'_, VmState<'_>>,
        locator: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.get_tx_nested_array_len(locator.read(memory)?)?)
    }

    fn get_current_ledger_obj_nested_array_len(
        caller: &mut Caller<'_, VmState<'_>>,
        locator: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.get_current_ledger_obj_nested_array_len(locator.read(memory)?)?)
    }

    fn get_ledger_obj_nested_array_len(
        caller: &mut Caller<'_, VmState<'_>>,
        cache_idx: i32,
        locator: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.get_ledger_obj_nested_array_len(cache_idx, locator.read(memory)?)?)
    }

    fn check_signature(
        caller: &mut Caller<'_, VmState<'_>>,
        message: InBytes,
        signature: InBytes,
        pubkey: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.check_signature(
            message.read(memory)?,
            signature.read(memory)?,
            pubkey.read(memory)?,
        )?)
    }

    fn account_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.account_keylet(account.read(memory)?, buf)
        })
    }

    fn amm_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        asset1: InBytes,
        asset2: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.amm_keylet(asset1.read(memory)?, asset2.read(memory)?, buf)
        })
    }

    fn check_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.check_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn credential_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        subject: InBytes,
        issuer: InBytes,
        credential_type: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.credential_keylet(
                subject.read(memory)?,
                issuer.read(memory)?,
                credential_type.read(memory)?,
                buf,
            )
        })
    }

    fn delegate_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        authorize: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.delegate_keylet(account.read(memory)?, authorize.read(memory)?, buf)
        })
    }

    fn deposit_preauth_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        authorize: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.deposit_preauth_keylet(account.read(memory)?, authorize.read(memory)?, buf)
        })
    }

    fn did_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.did_keylet(account.read(memory)?, buf)
        })
    }

    fn escrow_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.escrow_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn trust_line_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account1: InBytes,
        account2: InBytes,
        currency: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.trust_line_keylet(
                account1.read(memory)?,
                account2.read(memory)?,
                currency.read(memory)?,
                buf,
            )
        })
    }

    fn mptoken_issuance_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        issuer: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.mptoken_issuance_keylet(issuer.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn mptoken_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        mptid: InBytes,
        holder: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.mptoken_keylet(mptid.read(memory)?, holder.read(memory)?, buf)
        })
    }

    fn nftoken_offer_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.nftoken_offer_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn offer_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.offer_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn oracle_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        doc_id: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.oracle_keylet(account.read(memory)?, doc_id.read(memory)?, buf)
        })
    }

    fn paychannel_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        destination: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.paychannel_keylet(
                account.read(memory)?,
                destination.read(memory)?,
                seq.read(memory)?,
                buf,
            )
        })
    }

    fn permissioned_domain_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.permissioned_domain_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn signer_list_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.signer_list_keylet(account.read(memory)?, buf)
        })
    }

    fn ticket_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.ticket_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn vault_keylet(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        seq: InU32,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.vault_keylet(account.read(memory)?, seq.read(memory)?, buf)
        })
    }

    fn sha512_half(
        caller: &mut Caller<'_, VmState<'_>>,
        data: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.sha512_half(data.read(memory)?, buf)
        })
    }

    /// The one body with nothing to answer: the wasm function has no result to
    /// carry a code, so a malformed argument leaves the guest none the wiser and
    /// the host uncalled.
    fn trace(
        caller: &mut Caller<'_, VmState<'_>>,
        msg: InStr,
        data_type: TraceCode,
        data: InBytes,
    ) -> CallResult<()> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.trace(msg.read(memory)?, data_type.read()?, data.read(memory)?)?)
    }

    fn update_data(caller: &mut Caller<'_, VmState<'_>>, data: InBytes) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.update_data(data.read(memory)?)?)
    }

    fn get_nft(
        caller: &mut Caller<'_, VmState<'_>>,
        account: InBytes,
        nft_id: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_nft(account.read(memory)?, nft_id.read(memory)?, buf)
        })
    }

    fn get_nft_issuer(
        caller: &mut Caller<'_, VmState<'_>>,
        nft_id: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_nft_issuer(nft_id.read(memory)?, buf)
        })
    }

    fn get_nft_taxon(
        caller: &mut Caller<'_, VmState<'_>>,
        nft_id: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_nft_taxon(nft_id.read(memory)?, buf)
        })
    }

    fn get_nft_flags(caller: &mut Caller<'_, VmState<'_>>, nft_id: InBytes) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.get_nft_flags(nft_id.read(memory)?)?)
    }

    fn get_nft_transfer_fee(
        caller: &mut Caller<'_, VmState<'_>>,
        nft_id: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.get_nft_transfer_fee(nft_id.read(memory)?)?)
    }

    fn get_nft_sequence(
        caller: &mut Caller<'_, VmState<'_>>,
        nft_id: InBytes,
        out: OutBytes,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.get_nft_sequence(nft_id.read(memory)?, buf)
        })
    }

    fn float_from_int(
        caller: &mut Caller<'_, VmState<'_>>,
        x: i64,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| host.float_from_int(x, out, mode))
    }

    fn float_from_uint(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_from_uint(x.read(memory)?, buf, mode)
        })
    }

    fn float_from_stamount(
        caller: &mut Caller<'_, VmState<'_>>,
        amount: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_from_stamount(amount.read(memory)?, buf, mode)
        })
    }

    fn float_from_stnumber(
        caller: &mut Caller<'_, VmState<'_>>,
        number: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_from_stnumber(number.read(memory)?, buf, mode)
        })
    }

    fn float_to_int(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_to_int(x.read(memory)?, buf, mode)
        })
    }

    fn float_to_mant_exp(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        mantissa_out: OutBytes,
        exponent_out: OutBytes,
    ) -> CallResult<i32> {
        write_mant_exp(
            caller,
            mantissa_out,
            exponent_out,
            |host, memory, mantissa, exponent| {
                host.float_to_mant_exp(x.read(memory)?, mantissa, exponent)
            },
        )
    }

    fn float_from_mant_exp(
        caller: &mut Caller<'_, VmState<'_>>,
        mantissa: i64,
        exponent: i32,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_into(caller, out, |host, out| {
            host.float_from_mant_exp(mantissa, exponent, out, mode)
        })
    }

    fn float_compare(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        y: InBytes,
    ) -> CallResult<i32> {
        let memory = guest_memory(caller)?;
        let host = caller.data().host;
        Ok(host.float_compare(x.read(memory)?, y.read(memory)?)?)
    }

    fn float_add(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        y: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_add(x.read(memory)?, y.read(memory)?, buf, mode)
        })
    }

    fn float_subtract(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        y: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_subtract(x.read(memory)?, y.read(memory)?, buf, mode)
        })
    }

    fn float_multiply(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        y: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_multiply(x.read(memory)?, y.read(memory)?, buf, mode)
        })
    }

    fn float_divide(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        y: InBytes,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_divide(x.read(memory)?, y.read(memory)?, buf, mode)
        })
    }

    fn float_power(
        caller: &mut Caller<'_, VmState<'_>>,
        x: InBytes,
        n: i32,
        out: OutBytes,
        mode: i32,
    ) -> CallResult<i32> {
        write_buffered(caller, out, |host, memory, buf| {
            host.float_power(x.read(memory)?, n, buf, mode)
        })
    }
}
