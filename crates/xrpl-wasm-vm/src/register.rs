use crate::abi::{charged, read_borrowed, region, scratch_write, write_into};
use crate::vm::VmState;
use wasmi::{Caller, Linker};
use xrpl_host_functions::{HostError, HostFunctionSpec};

/// Import module namespace the guest imports host functions from
/// (`(import "host_lib" "ldgr_index" ...)`). The guest SDK and this fork's
/// fixtures spell it this way.
const HOST_MODULE: &str = "host_lib";

// ---------------------------------------------------------------------------
// Import registration
// ---------------------------------------------------------------------------

/// Register the PoC's host functions on `linker`, one per [`HostFunctionSpec`]
/// variant.
///
/// Driven by an exhaustive `match` over [`HostFunctionSpec::ALL`]: adding a
/// variant to the ABI won't compile until it has an arm here (that's the "can't
/// forget to register" guarantee). Each arm is one [`charged`] call — the sole
/// entry point for `charge`, and the one place a result is split between the
/// status the guest reads and the trap it cannot — wrapping a body that calls
/// straight into the [`xrpl_host_functions::HostFunctions`] trait object held in
/// the [`wasmi::Store`].
pub(crate) fn register_host_functions(
    linker: &mut Linker<VmState<'_>>,
) -> Result<(), wasmi::errors::LinkerError> {
    // TODO: think on how to make it better
    for &op in HostFunctionSpec::ALL {
        match op {
            HostFunctionSpec::GetLedgerSqn => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetLedgerSqn, |c| {
                        // The host writes the serialized sequence number
                        // straight into the guest output region; `write_into`
                        // owns the bounds/cap/buffer/transfer policy.
                        write_into(c, out_ptr, out_len, |host, out| host.get_ledger_sqn(out))
                    })
                },
            ),
            HostFunctionSpec::GetCurrentLedgerObjField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 field: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(
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
                    )
                },
            ),
            HostFunctionSpec::Sha512Half => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 data_ptr: i32,
                 data_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::Sha512Half, |c| {
                        // The input is borrowed straight out of guest memory and
                        // the digest is written to the run's scratch, which
                        // `scratch_write` copies to the guest after its
                        // bounds/cap/buffer/transfer policy passes.
                        scratch_write(c, out_ptr, out_len, |host, data, out| {
                            let input = region(data, data_ptr, data_len)?;
                            host.sha512_half(input, out)
                        })
                    })
                },
            ),
            HostFunctionSpec::Trace => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 data_ptr: i32,
                 data_len: i32,
                 as_hex: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::Trace, |c| {
                        // Read `msg`/`data` straight out of guest memory — the
                        // slices alias linear memory, no owned copy (`trace`
                        // returns nothing, so there's no output-aliasing worry).
                        let host = c.data().host;
                        let msg = read_borrowed(c, msg_ptr, msg_len)?;
                        let data = read_borrowed(c, data_ptr, data_len)?;
                        let msg = core::str::from_utf8(msg).map_err(|_| HostError::Decoding)?;
                        host.trace(msg, data, as_hex != 0)?;
                        Ok(0)
                    })
                },
            ),
            HostFunctionSpec::TraceNum => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 number: i64|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::TraceNum, |c| {
                        // `msg` aliases guest memory — no owned copy.
                        let host = c.data().host;
                        let msg = read_borrowed(c, msg_ptr, msg_len)?;
                        let msg = core::str::from_utf8(msg).map_err(|_| HostError::Decoding)?;
                        host.trace_num(msg, number)?;
                        Ok(0)
                    })
                },
            ),
        }?;
    }
    Ok(())
}
