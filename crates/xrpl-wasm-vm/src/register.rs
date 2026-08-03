use crate::abi::{charged, read_borrowed, region, scratch_write, write_into};
use crate::vm::VmState;
use wasmi::{Caller, Linker};
use xrpl_host_functions::{HostError, HostFunctionSpec};

/// The module name the guest imports under (`(import "host_lib" "ldgr_index" …)`),
/// as the guest SDK and this fork's fixtures spell it.
const HOST_MODULE: &str = "host_lib";

/// Register the host functions on `linker`, one per [`HostFunctionSpec`] variant.
///
/// The `match` is exhaustive over [`HostFunctionSpec::ALL`], so a variant added to
/// the ABI will not compile until it has an arm here — the "cannot forget to
/// register" guarantee. Every arm goes through [`charged`], which is what makes the
/// gas charge and the wire encoding unforgettable too.
pub(crate) fn register_host_functions(
    linker: &mut Linker<VmState<'_>>,
) -> Result<(), wasmi::errors::LinkerError> {
    // The arms are hand-written and repetitive by decision, not by neglect:
    // generating them needs the typed `link_*` shims, deferred until the C header
    // is generated from the same table (docs/claude/redesign_impl.md).
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
