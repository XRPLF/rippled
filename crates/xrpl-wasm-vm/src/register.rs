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
            HostFunctionSpec::GetLedgerObjField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 cache_idx: i32,
                 field: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetLedgerObjField, |c| {
                        let out = Region::new(out_ptr, out_len);
                        write_into(c, out, |host, out| {
                            host.get_ledger_obj_field(cache_idx, field, out)
                        })
                    })
                },
            ),
            HostFunctionSpec::GetTxNestedField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 loc_ptr: i32,
                 loc_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetTxNestedField, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let locator = Region::new(loc_ptr, loc_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.get_tx_nested_field(locator.read(data)?, buf)
                        })
                    })
                },
            ),
            HostFunctionSpec::GetCurrentLedgerObjNestedField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 loc_ptr: i32,
                 loc_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(
                        &mut caller,
                        HostFunctionSpec::GetCurrentLedgerObjNestedField,
                        |c| {
                            let out = Region::new(out_ptr, out_len);
                            let locator = Region::new(loc_ptr, loc_len);
                            write_buffered(c, out, |host, data, buf| {
                                host.get_current_ledger_obj_nested_field(locator.read(data)?, buf)
                            })
                        },
                    )
                },
            ),
            HostFunctionSpec::GetLedgerObjNestedField => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 cache_idx: i32,
                 loc_ptr: i32,
                 loc_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(
                        &mut caller,
                        HostFunctionSpec::GetLedgerObjNestedField,
                        |c| {
                            let out = Region::new(out_ptr, out_len);
                            let locator = Region::new(loc_ptr, loc_len);
                            write_buffered(c, out, |host, data, buf| {
                                host.get_ledger_obj_nested_field(
                                    cache_idx,
                                    locator.read(data)?,
                                    buf,
                                )
                            })
                        },
                    )
                },
            ),
            HostFunctionSpec::GetTxArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>, field: i32| -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetTxArrayLen, |c| {
                        c.data().host.get_tx_array_len(field)
                    })
                },
            ),
            HostFunctionSpec::GetCurrentLedgerObjArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>, field: i32| -> Result<i32, wasmi::Error> {
                    charged(
                        &mut caller,
                        HostFunctionSpec::GetCurrentLedgerObjArrayLen,
                        |c| c.data().host.get_current_ledger_obj_array_len(field),
                    )
                },
            ),
            HostFunctionSpec::GetLedgerObjArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 cache_idx: i32,
                 field: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetLedgerObjArrayLen, |c| {
                        c.data().host.get_ledger_obj_array_len(cache_idx, field)
                    })
                },
            ),
            HostFunctionSpec::GetTxNestedArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 loc_ptr: i32,
                 loc_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::GetTxNestedArrayLen, |c| {
                        let host = c.data().host;
                        let locator = read_borrowed(c, Region::new(loc_ptr, loc_len))?;
                        host.get_tx_nested_array_len(locator)
                    })
                },
            ),
            HostFunctionSpec::GetCurrentLedgerObjNestedArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 loc_ptr: i32,
                 loc_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(
                        &mut caller,
                        HostFunctionSpec::GetCurrentLedgerObjNestedArrayLen,
                        |c| {
                            let host = c.data().host;
                            let locator = read_borrowed(c, Region::new(loc_ptr, loc_len))?;
                            host.get_current_ledger_obj_nested_array_len(locator)
                        },
                    )
                },
            ),
            HostFunctionSpec::GetLedgerObjNestedArrayLen => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 cache_idx: i32,
                 loc_ptr: i32,
                 loc_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(
                        &mut caller,
                        HostFunctionSpec::GetLedgerObjNestedArrayLen,
                        |c| {
                            let host = c.data().host;
                            let locator = read_borrowed(c, Region::new(loc_ptr, loc_len))?;
                            host.get_ledger_obj_nested_array_len(cache_idx, locator)
                        },
                    )
                },
            ),
            HostFunctionSpec::CheckSignature => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 msg_ptr: i32,
                 msg_len: i32,
                 sig_ptr: i32,
                 sig_len: i32,
                 pk_ptr: i32,
                 pk_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::CheckSignature, |c| {
                        let host = c.data().host;
                        let message = read_borrowed(c, Region::new(msg_ptr, msg_len))?;
                        let signature = read_borrowed(c, Region::new(sig_ptr, sig_len))?;
                        let pubkey = read_borrowed(c, Region::new(pk_ptr, pk_len))?;
                        host.check_signature(message, signature, pubkey)
                    })
                },
            ),
            HostFunctionSpec::AccountKeylet => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 acc_ptr: i32,
                 acc_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::AccountKeylet, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let account = Region::new(acc_ptr, acc_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.account_keylet(account.read(data)?, buf)
                        })
                    })
                },
            ),
            HostFunctionSpec::AmmKeylet => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 a1_ptr: i32,
                 a1_len: i32,
                 a2_ptr: i32,
                 a2_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::AmmKeylet, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let asset1 = Region::new(a1_ptr, a1_len);
                        let asset2 = Region::new(a2_ptr, a2_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.amm_keylet(asset1.read(data)?, asset2.read(data)?, buf)
                        })
                    })
                },
            ),
            HostFunctionSpec::CheckKeylet => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 acc_ptr: i32,
                 acc_len: i32,
                 seq: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::CheckKeylet, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let account = Region::new(acc_ptr, acc_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.check_keylet(account.read(data)?, seq, buf)
                        })
                    })
                },
            ),
            HostFunctionSpec::CredentialKeylet => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 subj_ptr: i32,
                 subj_len: i32,
                 iss_ptr: i32,
                 iss_len: i32,
                 ct_ptr: i32,
                 ct_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::CredentialKeylet, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let subject = Region::new(subj_ptr, subj_len);
                        let issuer = Region::new(iss_ptr, iss_len);
                        let cred_type = Region::new(ct_ptr, ct_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.credential_keylet(
                                subject.read(data)?,
                                issuer.read(data)?,
                                cred_type.read(data)?,
                                buf,
                            )
                        })
                    })
                },
            ),
            HostFunctionSpec::DelegateKeylet => linker.func_wrap(
                HOST_MODULE,
                op.wasm_name(),
                |mut caller: Caller<'_, VmState<'_>>,
                 acc_ptr: i32,
                 acc_len: i32,
                 auth_ptr: i32,
                 auth_len: i32,
                 out_ptr: i32,
                 out_len: i32|
                 -> Result<i32, wasmi::Error> {
                    charged(&mut caller, HostFunctionSpec::DelegateKeylet, |c| {
                        let out = Region::new(out_ptr, out_len);
                        let account = Region::new(acc_ptr, acc_len);
                        let authorize = Region::new(auth_ptr, auth_len);
                        write_buffered(c, out, |host, data, buf| {
                            host.delegate_keylet(account.read(data)?, authorize.read(data)?, buf)
                        })
                    })
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
