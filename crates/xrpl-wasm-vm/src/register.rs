use crate::abi::{AbiRet, charged, read_borrowed, read_write, to_wasm_i32, write_into};
use crate::vm::VmState;
use wasmi::{Caller, Linker};
use xrpl_host_functions::{HostError, HostFunctionSpec};

/// Import module namespace the guest imports host functions from
/// (`(import "host" "ldgr_index" ...)`).
const HOST_MODULE: &str = "host";

// ---------------------------------------------------------------------------
// Import registration
// ---------------------------------------------------------------------------

/// Register the PoC's host functions on `linker`, one per [`HostFn`] variant.
///
/// Driven by an exhaustive `match` over [`HostFn::ALL`]: adding a variant to
/// the ABI won't compile until it has an arm here (that's the "can't forget to
/// register" guarantee). Each arm charges gas once via [`charged`] — the sole
/// entry point for `charge` — and marshals its wasm scalars through
/// [`AbiArg`]/[`AbiRet`] before calling straight into the [`HostFunctions`]
/// trait object held in the [`Store`].
pub(crate) fn register_host_functions(linker: &mut Linker<VmState<'_>>) -> Result<(), String> {
    fn link_err(e: wasmi::errors::LinkerError) -> String {
        format!("register import: {e}")
    }

    // TODO: think on how to make it better
    for &op in HostFunctionSpec::ALL {
        match op {
            HostFunctionSpec::GetLedgerSqn => linker.func_wrap(
                HOST_MODULE,
                op.spec().name,
                |mut caller: Caller<'_, VmState<'_>>, out_ptr: i32, out_len: i32| -> i32 {
                    to_wasm_i32(charged(&mut caller, HostFunctionSpec::GetLedgerSqn, |c| {
                        // The host writes the serialized sequence number
                        // straight into the guest output region; `write_into`
                        // owns the bounds/cap/buffer/transfer policy.
                        write_into(c, out_ptr, out_len, |host, out| host.get_ledger_sqn(out))
                    }))
                },
            ),
            HostFunctionSpec::GetCurrentLedgerObjField => linker.func_wrap(
                HOST_MODULE,
                op.spec().name,
                |mut caller: Caller<'_, VmState<'_>>,
                 field: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> i32 {
                    to_wasm_i32(charged(
                        &mut caller,
                        HostFunctionSpec::GetCurrentLedgerObjField,
                        |c| {
                            // The host writes the field's bytes straight into
                            // the guest output region (no owned `Vec` in
                            // between); `write_into` owns the policy.
                            write_into(c, out_ptr, out_len, |host, out| {
                                host.get_current_ledger_obj_field(field, out)
                            })
                        },
                    ))
                },
            ),
            HostFunctionSpec::Sha512Half => linker.func_wrap(
                HOST_MODULE,
                op.spec().name,
                |mut caller: Caller<'_, VmState<'_>>,
                 data_ptr: i32,
                 data_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> i32 {
                    to_wasm_i32(charged(&mut caller, HostFunctionSpec::Sha512Half, |c| {
                        // Input copied into a stack buffer (no heap), output
                        // written straight into guest memory; `read_write`
                        // owns the read/write bounds/cap/transfer policy.
                        read_write(
                            c,
                            data_ptr,
                            data_len,
                            out_ptr,
                            out_len,
                            |host, data, out| host.sha512_half(data, out),
                        )
                    }))
                },
            ),
            HostFunctionSpec::Trace => linker.func_wrap(
                HOST_MODULE,
                op.spec().name,
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 data_ptr: i32,
                 data_len: i32,
                 as_hex: i32|
                 -> i32 {
                    to_wasm_i32(charged(&mut caller, HostFunctionSpec::Trace, |c| {
                        // Read `msg`/`data` straight out of guest memory — the
                        // slices alias linear memory, no owned copy (`trace`
                        // returns nothing, so there's no output-aliasing worry).
                        let host = c.data().host;
                        let msg = read_borrowed(c, msg_ptr, msg_len)?;
                        let data = read_borrowed(c, data_ptr, data_len)?;
                        let msg = core::str::from_utf8(msg).map_err(|_| HostError::Decoding)?;
                        host.trace(msg, data, as_hex != 0)?;
                        <() as AbiRet>::write((), c, ())
                    }))
                },
            ),
            HostFunctionSpec::TraceNum => linker.func_wrap(
                HOST_MODULE,
                op.spec().name,
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 number: i64|
                 -> i32 {
                    to_wasm_i32(charged(&mut caller, HostFunctionSpec::TraceNum, |c| {
                        // `msg` aliases guest memory — no owned copy.
                        let host = c.data().host;
                        let msg = read_borrowed(c, msg_ptr, msg_len)?;
                        let msg = core::str::from_utf8(msg).map_err(|_| HostError::Decoding)?;
                        host.trace_num(msg, number)?;
                        <() as AbiRet>::write((), c, ())
                    }))
                },
            ),
        }
        .map_err(link_err)?;
    }
    Ok(())
}
