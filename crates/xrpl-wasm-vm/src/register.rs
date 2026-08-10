use crate::abi::{charged, read_borrowed, write_buffered, write_into};
use crate::region::Region;
use crate::vm::VmState;
use wasmi::{Caller, Linker};
use xrpl_host_functions::{HostError, HostFunctionSpec};

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
            HostFunctionSpec::GetParentLedgerTime => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetParentLedgerTime, |c| {
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| host.get_parent_ledger_time(out))
                    })
                },
            ),
            HostFunctionSpec::GetParentLedgerHash => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetParentLedgerHash, |c| {
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| host.get_parent_ledger_hash(out))
                    })
                },
            ),
            HostFunctionSpec::GetBaseFee => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetBaseFee, |c| {
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| host.get_base_fee(out))
                    })
                },
            ),
            HostFunctionSpec::IsAmendmentEnabled => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 ptr: i32,
                 len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::IsAmendmentEnabled, |c| {
                        let host = c.data().host;
                        let amendment = read_borrowed(c, Region::new(ptr, len))?;
                        host.is_amendment_enabled(amendment)
                    })
                },
            ),
            HostFunctionSpec::CacheLedgerObj => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 id_ptr: i32,
                 id_len: i32,
                 cache_idx: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::CacheLedgerObj, |c| {
                        let host = c.data().host;
                        let obj_id = read_borrowed(c, Region::new(id_ptr, id_len))?;
                        host.cache_ledger_obj(obj_id, cache_idx)
                    })
                },
            ),
            HostFunctionSpec::GetTxField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 field: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetTxField, |c| {
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| host.get_tx_field(field, out))
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
                        let msg = read_borrowed(c, Region::new(msg_ptr, msg_len))?;
                        let data = read_borrowed(c, Region::new(data_ptr, data_len))?;
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
                        let msg = read_borrowed(c, Region::new(msg_ptr, msg_len))?;
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
