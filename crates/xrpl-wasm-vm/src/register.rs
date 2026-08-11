use crate::abi::{charged, charged_unreported, read_borrowed, write_buffered, write_into};
use crate::region::Region;
use crate::vm::VmState;
use wasmi::{Caller, Linker};
use xrpl_host_functions::{HostError, HostFunctionSpec, TraceDataType};

/// The module name the guest imports under (`(import "host_lib" "ldgr_index" …)`),
/// as the guest SDK and this fork's fixtures spell it.
pub(crate) const HOST_MODULE: &str = "host_lib";

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
    // is generated from the same table.
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
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| host.get_ledger_sqn(out))
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
                            let out = Region::new(out_ptr, out_len);
                            write_into(c, out, |host, out| {
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
                        let out = Region::new(out_ptr, out_len);
                        let input = Region::new(data_ptr, data_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.sha512_half(input.read(data)?, buf)
                        })
                    })
                },
            ),
            // The one arm with no result: the wasm function is `(param i32 i32 i32 i32
            // i32)` and nothing more, so a malformed call is dropped rather than
            // answered — an unreadable region, a `msg` that is not UTF-8 and a
            // `data_type` naming no rendering all leave the guest none the wiser, and
            // the host uncalled.
            //
            // Also the one arm whose parameters are not the declaration's order:
            // `data_type` arrives third, between the two regions, as xrpld and the
            // guest stdlib spell it. The wasm order is this closure's; the declaration
            // order is the call's.
            HostFunctionSpec::Trace => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 data_type: i32,
                 data_ptr: i32,
                 data_len: i32|
                 -> Result<(), wasmi::Error> {
                    charged_unreported(&mut caller, HostFunctionSpec::Trace, |c| {
                        let host = c.data().host;
                        let msg = read_borrowed(c, Region::new(msg_ptr, msg_len))?;
                        let msg =
                            core::str::from_utf8(msg).map_err(|_| HostError::InvalidParams)?;
                        let data_type =
                            TraceDataType::from_code(data_type).ok_or(HostError::InvalidParams)?;
                        let data = read_borrowed(c, Region::new(data_ptr, data_len))?;
                        Ok(host.trace(msg, data, data_type)?)
                    })
                },
            ),
        }?;
    }
    Ok(())
}
