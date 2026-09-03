//! The wasmi registration, generated from the declarations the ABI table is
//! generated from — so the closure a guest links against cannot disagree with the
//! signature preflight screens it by.
//!
//! What is emitted here is a `macro_rules!` rather than the registration itself.
//! The registration names `wasmi::Linker` and the VM's own store type, and the
//! crate this expansion lands in is `no_std`, zero-dependency and links into the
//! guest — but a `macro_rules!` body is inert tokens until someone expands it, so
//! carrying the glue costs that crate no dependency and no feature flag.
//!
//! **The body resolves in two crates at once**, which is what makes one macro
//! enough. `$crate::HostFunctionSpec` is the ABI crate wherever the macro is
//! expanded; every other name in the body resolves at the *expansion site*. Those
//! names are the hidden contract this file keeps, and the emitted macro's own doc
//! comment is where a reader finds them listed.
//!
//! This is the one file that knows an engine's calling convention: how a region
//! arrives as two wasm parameters, where the gas charge goes, and which helper a
//! result-less function takes. A second engine would be a second file like it.

use proc_macro2::TokenStream;
use quote::{ToTokens, format_ident, quote};
use syn::Ident;

use crate::lowering::{ParamType, ResultType, WasmValType};
use crate::parsed_host_function::{Param, ParsedHostFunction};

/// The `wasmi_glue!` macro: the trait a VM implements one body per host function
/// in, and the registration that hands each of them to a `Linker`.
pub(crate) fn wasmi_glue(functions: &[ParsedHostFunction]) -> TokenStream {
    let bodies = functions.iter().map(body_declaration);
    let registrations = functions.iter().map(registration);

    quote! {
        /// Expands to the wasmi glue for this ABI: the `HostFunctionBodies` trait
        /// and `register_host_functions`, at the scope it is called in.
        ///
        /// One call site, in `xrpl-wasm-vm`'s `register.rs`, which then implements
        /// the trait once — the bodies are all that is left hand-written, and a
        /// declaration added to the ABI is a missing trait item rather than a
        /// forgotten registration.
        ///
        /// # What it expects in scope
        ///
        /// Everything the expansion names but `$crate::HostFunctionSpec` resolves
        /// where it is expanded: `HOST_MODULE`, the store type `VmState`, the
        /// charging helpers `charged` and `charged_unreported` with their
        /// `CallResult`, and the argument types `InBytes`, `InStr`, `InU32`,
        /// `OutBytes` and `TraceCode`.
        #[macro_export]
        macro_rules! wasmi_glue {
            () => {
                /// One body per host function: what the engine runs once the call's
                /// gas is charged and its arguments are off the wire.
                ///
                /// The methods take no receiver, so a registered closure captures
                /// nothing at all — which is what satisfies wasmi's
                /// `Fn + Send + Sync + 'static` bound, and why the implementor
                /// itself need not be `'static`.
                ///
                /// A body is handed the caller and its arguments in their wire
                /// form, and answers what the guest is told. It does **not** charge
                /// gas: that belongs to the generated closure, so it cannot be
                /// forgotten or charged twice.
                pub(crate) trait HostFunctionBodies {
                    #(#bodies)*
                }

                /// Register every host function on `linker`, one `func_wrap` per
                /// declaration.
                ///
                /// The parameter list of each closure is the ABI's derived wasm
                /// signature, so this is not a second statement of it: an import
                /// that passes [`crate::check`] links here by construction.
                pub(crate) fn register_host_functions<B: HostFunctionBodies>(
                    linker: &mut ::wasmi::Linker<VmState<'_>>,
                ) -> ::core::result::Result<(), ::wasmi::errors::LinkerError> {
                    #(#registrations)*
                    Ok(())
                }
            };
        }
    }
}

/// `fn check_keylet(caller: &mut Caller<'_, VmState<'_>>, account: InBytes, seq:
/// InU32, out: OutBytes) -> CallResult<i32>;`
fn body_declaration(function: &ParsedHostFunction) -> TokenStream {
    let name = &function.signature.ident;
    let params = function.params().iter().map(|param| {
        let name = &param.name;
        let ty = argument_type(param.ty);
        quote! { #name: #ty }
    });
    let answer = answer_type(function.result());

    quote! {
        fn #name(
            caller: &mut ::wasmi::Caller<'_, VmState<'_>>,
            #(#params),*
        ) -> #answer;
    }
}

/// One `linker.func_wrap(…)?;`: the wasm signature as the closure's parameters,
/// the gas charge around the call, and the body between them.
fn registration(function: &ParsedHostFunction) -> TokenStream {
    let body = &function.signature.ident;
    let spec = spec_path(function);
    let params = function
        .params()
        .iter()
        .flat_map(closure_params)
        .map(|(name, ty)| quote! { #name: #ty });
    let arguments = function.params().iter().map(lift);
    let (answer, charge) = match function.result() {
        ResultType::BufferLength | ResultType::Value => (quote!(i32), quote!(charged)),
        ResultType::Nothing => (quote!(()), quote!(charged_unreported)),
    };

    quote! {
        linker.func_wrap(
            HOST_MODULE,
            #spec.wasm_name(),
            |mut caller: ::wasmi::Caller<'_, VmState<'_>>, #(#params),*|
             -> ::core::result::Result<#answer, ::wasmi::Error> {
                #charge(&mut caller, #spec, |caller| {
                    B::#body(caller, #(#arguments),*)
                })
            },
        )?;
    }
}

/// `$crate::HostFunctionSpec::CheckKeylet` — the one name the expansion reaches
/// back into the ABI crate for, and the reason `$crate` is load-bearing here.
fn spec_path(function: &ParsedHostFunction) -> TokenStream {
    let variant = &function.variant;
    quote! { $crate::HostFunctionSpec::#variant }
}

/// The wasm parameters one declared parameter becomes, named and typed.
///
/// The names are the declaration's own — a region's pair is `{name}_ptr` and
/// `{name}_len` — and the types come from [`ParamType::as_wasm_params`], so the
/// arity a closure is registered at *is* the derived arity rather than a second
/// statement of it.
fn closure_params(param: &Param) -> Vec<(Ident, TokenStream)> {
    wasm_names(param)
        .into_iter()
        .zip(param.ty.as_wasm_params())
        .map(|(name, val_type)| (name, rust_type(*val_type)))
        .collect()
}

/// `[account_ptr, account_len]` for a region, `[field]` for a scalar.
///
/// The one thing a declaration could do to break this is take both `x: &[u8]` and
/// `x_ptr`, whose generated names would collide; rustc says so at the
/// `wasmi_glue!()` call site rather than at the declaration, which is a poor
/// message and nothing worse.
fn wasm_names(param: &Param) -> Vec<Ident> {
    if param.ty.is_region() {
        return vec![
            format_ident!("{}_ptr", param.name),
            format_ident!("{}_len", param.name),
        ];
    }
    vec![param.name.clone()]
}

/// The argument a body is handed, built from the wasm parameters it arrived as:
/// `InBytes::new(account_ptr, account_len)`, or the scalar itself.
fn lift(param: &Param) -> TokenStream {
    let names = wasm_names(param);
    let ty = argument_type(param.ty);

    match param.ty {
        ParamType::I32 | ParamType::I64 => param.name.to_token_stream(),
        ParamType::TraceDataType
        | ParamType::InBytes
        | ParamType::InStr
        | ParamType::InU32
        | ParamType::OutBytes => quote! { #ty::new(#(#names),*) },
    }
}

/// The type a body takes a declared parameter as: a wasm scalar spelled as
/// itself, and everything else the argument type that carries its shape and its
/// direction.
///
/// This is what makes an input region used as an output one a compile error, at
/// the `impl` and naming both types.
fn argument_type(param: ParamType) -> TokenStream {
    match param {
        ParamType::I32 => quote!(i32),
        ParamType::I64 => quote!(i64),
        ParamType::TraceDataType => quote!(TraceCode),
        ParamType::InBytes => quote!(InBytes),
        ParamType::InStr => quote!(InStr),
        ParamType::InU32 => quote!(InU32),
        ParamType::OutBytes => quote!(OutBytes),
    }
}

/// What a body answers: the value the guest is told, or nothing at all for the
/// function whose whole effect is on the host.
fn answer_type(result: ResultType) -> TokenStream {
    match result {
        ResultType::BufferLength | ResultType::Value => quote!(CallResult<i32>),
        ResultType::Nothing => quote!(CallResult<()>),
    }
}

/// A wasm value type as a closure parameter is spelled.
///
/// Not [`WasmValType`]'s own `ToTokens`, which spells the ABI crate's *variant*
/// for the table; here the same value is a Rust type.
fn rust_type(val_type: WasmValType) -> TokenStream {
    match val_type {
        WasmValType::I32 => quote!(i32),
        WasmValType::I64 => quote!(i64),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use syn::parse_quote;

    fn parsed(function: syn::TraitItemFn) -> ParsedHostFunction {
        ParsedHostFunction::parse(function).expect("the declaration should parse")
    }

    /// The declaration whose declared and wasm parameter lists differ most:
    /// `account` and `out` are a `(ptr, len)` pair each, and `seq` — which reads
    /// like a scalar — is a third. Three arguments to the body, six on the wire,
    /// and the glue is what keeps the two lists in step.
    #[test]
    fn lowers_a_declaration_to_a_body_and_a_registration() {
        let keylet = parsed(parse_quote! {
            #[gas = 350]
            #[wasm_name = "check_id"]
            fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;
        });

        assert_eq!(
            body_declaration(&keylet).to_string(),
            "fn check_keylet (caller : & mut :: wasmi :: Caller < '_ , VmState < '_ >> , \
             account : InBytes , seq : InU32 , out : OutBytes) -> CallResult < i32 > ;"
        );

        assert_eq!(
            registration(&keylet).to_string(),
            "linker . func_wrap (HOST_MODULE , \
             $ crate :: HostFunctionSpec :: CheckKeylet . wasm_name () , \
             | mut caller : :: wasmi :: Caller < '_ , VmState < '_ >> , \
             account_ptr : i32 , account_len : i32 , seq_ptr : i32 , seq_len : i32 , \
             out_ptr : i32 , out_len : i32 | \
             -> :: core :: result :: Result < i32 , :: wasmi :: Error > \
             { charged (& mut caller , $ crate :: HostFunctionSpec :: CheckKeylet , | caller | \
             { B :: check_keylet (caller , InBytes :: new (account_ptr , account_len) , \
             InU32 :: new (seq_ptr , seq_len) , OutBytes :: new (out_ptr , out_len)) }) } ,) ? ;"
        );
    }

    /// A wasm scalar is passed through as itself, in declaration order: no pair,
    /// no argument type, and an `i64` that stays one.
    #[test]
    fn passes_the_wasm_scalars_through_untouched() {
        let from_int = parsed(parse_quote! {
            #[gas = 100]
            #[wasm_name = "float_from_int"]
            fn float_from_int(&self, x: i64, out: &mut [u8], mode: i32) -> HostResult<usize>;
        });

        assert_eq!(
            body_declaration(&from_int).to_string(),
            "fn float_from_int (caller : & mut :: wasmi :: Caller < '_ , VmState < '_ >> , \
             x : i64 , out : OutBytes , mode : i32) -> CallResult < i32 > ;"
        );

        let registration = registration(&from_int).to_string();
        assert!(
            registration.contains(
                "| mut caller : :: wasmi :: Caller < '_ , VmState < '_ >> , \
                 x : i64 , out_ptr : i32 , out_len : i32 , mode : i32 |"
            ),
            "{registration}"
        );
        assert!(
            registration.contains(
                "B :: float_from_int (caller , x , OutBytes :: new (out_ptr , out_len) , mode)"
            ),
            "{registration}"
        );
    }

    /// The function that answers nothing: no wasm result, and the charging helper
    /// that has nowhere to report a soft error. Derived from the declared
    /// `HostResult<()>` rather than named as a special case.
    #[test]
    fn a_declaration_that_answers_nothing_takes_the_other_charge() {
        let trace = parsed(parse_quote! {
            #[gas = 30]
            #[wasm_name = "trace"]
            fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()>;
        });

        assert_eq!(
            body_declaration(&trace).to_string(),
            "fn trace (caller : & mut :: wasmi :: Caller < '_ , VmState < '_ >> , \
             msg : InStr , data_type : TraceCode , data : InBytes) -> CallResult < () > ;"
        );

        let registration = registration(&trace).to_string();
        assert!(
            registration.contains(":: core :: result :: Result < () , :: wasmi :: Error >"),
            "{registration}"
        );
        assert!(
            registration.contains("charged_unreported (& mut caller"),
            "{registration}"
        );
        assert!(
            registration.contains(
                "B :: trace (caller , InStr :: new (msg_ptr , msg_len) , \
                 TraceCode :: new (data_type) , InBytes :: new (data_ptr , data_len))"
            ),
            "{registration}"
        );
    }

    /// The two worlds the macro body resolves in, pinned as tokens: the ABI crate
    /// through `$crate`, and one engine by name. `names_no_crate_of_its_own` holds
    /// the *other* half of the expansion to naming neither.
    #[test]
    fn reaches_the_abi_crate_through_dollar_crate_and_the_engine_by_name() {
        let glue = wasmi_glue(&[parsed(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        })])
        .to_string();

        assert!(
            glue.contains("$ crate :: HostFunctionSpec :: GetLedgerSqn"),
            "{glue}"
        );
        assert!(glue.contains(":: wasmi :: Linker"), "{glue}");
        assert!(!glue.contains("xrpl_host_functions"), "{glue}");
    }
}
