//! The wasmi registration, generated from the declarations the ABI table is
//! generated from — so the closure a guest links against cannot disagree with the
//! signature preflight screens it by.
//!
//! Emitted as a `macro_rules!` rather than as the registration itself, because the
//! crate the expansion lands in is `no_std`, zero-dependency and links into the
//! guest, and a `macro_rules!` body is inert tokens until someone expands it.
//!
//! The body therefore resolves in two crates at once and names nothing free:
//! `$crate` is the ABI crate, `$env` the module the caller passes in, and every
//! other path starts at `::wasmi` or `::core`. `$env` is matched as an `ident`
//! because `$env:path` used as `$env::Foo` is `error: missing angle brackets in
//! associated item path`.
//!
//! The one file that knows an engine's calling convention: how a region arrives as
//! two wasm parameters, where the gas charge goes, and which helper a result-less
//! function takes. A second engine would be a second file like it.

use proc_macro2::TokenStream;
use quote::{ToTokens, quote};

use crate::lowering::{ResultType, WasmValType};
use crate::parsed_host_function::{Param, ParsedHostFunction};

/// The `wasmi_glue!` macro: the trait a VM implements one body per host function
/// in, and the registration that hands each of them to a `Linker`.
pub(crate) fn wasmi_glue(functions: &[ParsedHostFunction]) -> TokenStream {
    let bodies = functions.iter().map(body_declaration);
    let registrations = functions.iter().map(registration);
    let assertions = charging_assertions();
    let env = env();

    quote! {
        /// Expands to the wasmi glue for this ABI: the `HostFunctionBodies` trait
        /// and `register_host_functions`, at the scope it is called in. A
        /// declaration added to the ABI is then a missing trait item rather than a
        /// forgotten registration.
        ///
        /// `$env` names a module holding everything the expansion reaches for on
        /// the engine's side, since this crate can name none of it: the store type
        /// `VmState`, the charging helpers `charged` and `charged_unreported` with
        /// their `CallResult`, and the argument types `InBytes`, `InStr`, `InU32`,
        /// `OutBytes` and `TraceCode`.
        ///
        /// ```ignore
        /// mod glue_env {
        ///     pub(crate) use crate::abi::{CallResult, charged, charged_unreported};
        ///     pub(crate) use crate::args::{InBytes, InStr, InU32, OutBytes, TraceCode};
        ///     pub(crate) use crate::vm::VmState;
        /// }
        ///
        /// xrpl_host_functions::wasmi_glue!(glue_env);
        /// ```
        ///
        /// The module supplies the spellings; the shapes are pinned by the
        /// expansion. Each argument type implements [`FromWasmRegion`] or
        /// [`FromWasmScalar`] — which one is the ABI's decision, so a declared
        /// `u32` is a region — and each charging helper's signature is asserted
        /// against a `const _`.
        #[cfg(feature = "wasmi_glue")]
        #[macro_export]
        macro_rules! wasmi_glue {
            ($env:ident) => {
                /// One body per host function: what the engine runs once the call's
                /// gas is charged and its arguments are off the wire.
                ///
                /// The methods take no receiver, so a registered closure captures
                /// nothing — which is what satisfies wasmi's
                /// `Fn + Send + Sync + 'static` bound, and why the implementor
                /// itself need not be `'static`. A body does not charge gas; the
                /// generated closure does, so it cannot be forgotten or charged
                /// twice.
                pub(crate) trait HostFunctionBodies {
                    #(#bodies)*
                }

                /// Register every host function on `linker`, one `func_wrap` per
                /// declaration, at the ABI's derived wasm signature — so an import
                /// that passes [`crate::check`] links here by construction.
                pub(crate) fn register_host_functions<B: HostFunctionBodies>(
                    linker: &mut ::wasmi::Linker<#env::VmState<'_>>,
                ) -> ::core::result::Result<(), ::wasmi::errors::LinkerError> {
                    #(#registrations)*
                    Ok(())
                }

                #assertions
            };
        }
    }
}

/// The macro argument every engine-side path is qualified by.
fn env() -> TokenStream {
    quote!($env)
}

/// The signature of each charging helper, pinned as a `const _` the expansion
/// carries: the one part of the contract neither `$env` nor the argument traits
/// state.
///
/// Its value is the diagnostic. A changed helper is already a type error at the
/// call, but there it is failed inference inside a generated closure and here it
/// is one line stating the signature that was expected.
fn charging_assertions() -> TokenStream {
    let env = env();
    let assertion = |helper: TokenStream, answer: TokenStream| {
        quote! {
            const _: fn(
                &mut ::wasmi::Caller<'_, #env::VmState<'_>>,
                $crate::HostFunctionSpec,
                fn(&mut ::wasmi::Caller<'_, #env::VmState<'_>>) -> #env::CallResult<#answer>,
            ) -> ::core::result::Result<#answer, ::wasmi::Error> = #env::#helper;
        }
    };

    let reported = assertion(quote!(charged), quote!(i32));
    let unreported = assertion(quote!(charged_unreported), quote!(()));

    quote! {
        #reported
        #unreported
    }
}

/// `fn check_keylet(caller: &mut Caller<'_, $env::VmState<'_>>, account:
/// $env::InBytes, seq: $env::InU32, out: $env::OutBytes) ->
/// $env::CallResult<i32>;`
fn body_declaration(function: &ParsedHostFunction) -> TokenStream {
    let env = env();
    let name = &function.signature.ident;
    let params = function.params().iter().map(|param| {
        let name = &param.name;
        let ty = param.ty.argument_type(&env);
        quote! { #name: #ty }
    });
    let answer = answer_type(function.result());

    quote! {
        fn #name(
            caller: &mut ::wasmi::Caller<'_, #env::VmState<'_>>,
            #(#params),*
        ) -> #answer;
    }
}

/// One `linker.func_wrap(…)?;`: the wasm signature as the closure's parameters,
/// the gas charge around the call, and the body between them.
fn registration(function: &ParsedHostFunction) -> TokenStream {
    let env = env();
    let body = &function.signature.ident;
    let spec = spec_path(function);
    let params = function.params().iter().flat_map(closure_params);
    let arguments = function.params().iter().map(lift);
    let (answer, charge) = match function.result() {
        ResultType::BufferLength | ResultType::Value => (quote!(i32), quote!(#env::charged)),
        ResultType::Nothing => (quote!(()), quote!(#env::charged_unreported)),
    };

    quote! {
        linker.func_wrap(
            $crate::HOST_MODULE,
            #spec.wasm_name(),
            |mut caller: ::wasmi::Caller<'_, #env::VmState<'_>>, #(#params),*|
             -> ::core::result::Result<#answer, ::wasmi::Error> {
                #charge(&mut caller, #spec, |caller| {
                    B::#body(caller, #(#arguments),*)
                })
            },
        )?;
    }
}

/// `$crate::HostFunctionSpec::CheckKeylet` — the one name the expansion reaches
/// back into the ABI crate for.
fn spec_path(function: &ParsedHostFunction) -> TokenStream {
    let variant = &function.variant;
    quote! { $crate::HostFunctionSpec::#variant }
}

/// One declared parameter as the closure declares it: `account_ptr: i32,
/// account_len: i32`, or `field: i32`.
///
/// Names and types both come from the lowering, so the arity a closure is
/// registered at *is* the derived arity.
fn closure_params(param: &Param) -> Vec<TokenStream> {
    param
        .ty
        .wasm_names(&param.name)
        .into_iter()
        .zip(param.ty.as_wasm_params())
        .map(|(name, val_type)| {
            let ty = rust_type(*val_type);
            quote! { #name: #ty }
        })
        .collect()
}

/// The argument a body is handed, built from the wasm parameters it arrived as:
/// `<$env::InBytes as $crate::FromWasmRegion>::from_wasm(account_ptr,
/// account_len)`, or the scalar itself.
///
/// Qualified rather than an inherent call, so the arity comes from the trait the
/// lowering chose: an argument type implementing the other one is an unsatisfied
/// bound named at the type, where `Ty::from_wasm(a, b)` would be an unrelated
/// arity error named here.
fn lift(param: &Param) -> TokenStream {
    let Some(argument_trait) = param.ty.argument_trait() else {
        return param.name.to_token_stream();
    };
    let ty = param.ty.argument_type(&env());
    let names = param.ty.wasm_names(&param.name);

    quote! { <#ty as $crate::#argument_trait>::from_wasm(#(#names),*) }
}

/// What a body answers: the value the guest is told, or nothing at all for the
/// function whose whole effect is on the host.
fn answer_type(result: ResultType) -> TokenStream {
    let env = env();
    match result {
        ResultType::BufferLength | ResultType::Value => quote!(#env::CallResult<i32>),
        ResultType::Nothing => quote!(#env::CallResult<()>),
    }
}

/// A wasm value type as a closure parameter spells it — a Rust type, not
/// [`WasmValType`]'s own `ToTokens`, which spells the ABI crate's variant.
fn rust_type(val_type: WasmValType) -> TokenStream {
    match val_type {
        WasmValType::I32 => quote!(i32),
        WasmValType::I64 => quote!(i64),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use proc_macro2::{Delimiter, Group, TokenTree};
    use syn::parse_quote;

    fn parsed(function: syn::TraitItemFn) -> ParsedHostFunction {
        ParsedHostFunction::parse(function).expect("the declaration should parse")
    }

    /// The declaration whose declared and wasm parameter lists differ most:
    /// `account`, `out` and `seq` are a `(ptr, len)` pair each, so three arguments
    /// to the body and six on the wire.
    #[test]
    fn lowers_a_declaration_to_a_body_and_a_registration() {
        let keylet = parsed(parse_quote! {
            #[gas = 350]
            #[wasm_name = "check_id"]
            fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;
        });

        assert_eq!(
            body_declaration(&keylet).to_string(),
            "fn check_keylet (caller : & mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
             account : $ env :: InBytes , seq : $ env :: InU32 , out : $ env :: OutBytes) \
             -> $ env :: CallResult < i32 > ;"
        );

        assert_eq!(
            registration(&keylet).to_string(),
            "linker . func_wrap ($ crate :: HOST_MODULE , \
             $ crate :: HostFunctionSpec :: CheckKeylet . wasm_name () , \
             | mut caller : :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
             account_ptr : i32 , account_len : i32 , seq_ptr : i32 , seq_len : i32 , \
             out_ptr : i32 , out_len : i32 | \
             -> :: core :: result :: Result < i32 , :: wasmi :: Error > \
             { $ env :: charged (& mut caller , \
             $ crate :: HostFunctionSpec :: CheckKeylet , | caller | \
             { B :: check_keylet (caller , \
             < $ env :: InBytes as $ crate :: FromWasmRegion > \
             :: from_wasm (account_ptr , account_len) , \
             < $ env :: InU32 as $ crate :: FromWasmRegion > :: from_wasm (seq_ptr , seq_len) , \
             < $ env :: OutBytes as $ crate :: FromWasmRegion > \
             :: from_wasm (out_ptr , out_len)) }) } ,) ? ;"
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
            "fn float_from_int (caller : & mut :: wasmi :: Caller < '_ , \
             $ env :: VmState < '_ >> , \
             x : i64 , out : $ env :: OutBytes , mode : i32) -> $ env :: CallResult < i32 > ;"
        );

        let registration = registration(&from_int).to_string();
        assert!(
            registration.contains(
                "| mut caller : :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
                 x : i64 , out_ptr : i32 , out_len : i32 , mode : i32 |"
            ),
            "{registration}"
        );
        assert!(
            registration.contains(
                "B :: float_from_int (caller , x , \
                 < $ env :: OutBytes as $ crate :: FromWasmRegion > \
                 :: from_wasm (out_ptr , out_len) , mode)"
            ),
            "{registration}"
        );
    }

    /// The function that answers nothing takes the other charging helper, derived
    /// from its declared `HostResult<()>` rather than named as a special case.
    /// Its `TraceCode` is also the only place `FromWasmScalar` is reached for.
    #[test]
    fn a_declaration_that_answers_nothing_takes_the_other_charge() {
        let trace = parsed(trace_declaration());

        assert_eq!(
            body_declaration(&trace).to_string(),
            "fn trace (caller : & mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
             msg : $ env :: InStr , data_type : $ env :: TraceCode , data : $ env :: InBytes) \
             -> $ env :: CallResult < () > ;"
        );

        let registration = registration(&trace).to_string();
        assert!(
            registration.contains(":: core :: result :: Result < () , :: wasmi :: Error >"),
            "{registration}"
        );
        assert!(
            registration.contains("$ env :: charged_unreported (& mut caller"),
            "{registration}"
        );
        assert!(
            registration.contains(
                "B :: trace (caller , \
                 < $ env :: InStr as $ crate :: FromWasmRegion > :: from_wasm (msg_ptr , msg_len) , \
                 < $ env :: TraceCode as $ crate :: FromWasmScalar > :: from_wasm (data_type) , \
                 < $ env :: InBytes as $ crate :: FromWasmRegion > \
                 :: from_wasm (data_ptr , data_len))"
            ),
            "{registration}"
        );
    }

    /// The two worlds the macro body resolves in: the ABI crate through `$crate`,
    /// and one engine by name. `names_no_crate_of_its_own` holds the ABI half of
    /// the expansion to naming neither.
    #[test]
    fn reaches_the_abi_crate_through_dollar_crate_and_the_engine_by_name() {
        let glue = code(wasmi_glue(&[parsed(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        })]));

        assert!(
            glue.contains("$ crate :: HostFunctionSpec :: GetLedgerSqn"),
            "{glue}"
        );
        assert!(glue.contains("$ crate :: HOST_MODULE"), "{glue}");
        assert!(glue.contains(":: wasmi :: Linker"), "{glue}");
        assert!(!glue.contains("xrpl_host_functions"), "{glue}");
    }

    /// Every engine-side item is reached through the module the macro is handed: a
    /// bare name would resolve against whatever the call site has in scope.
    ///
    /// `charged` covers `charged_unreported`, being its prefix.
    #[test]
    fn names_the_engine_s_own_items_only_through_the_module_it_is_handed() {
        let glue = code(wasmi_glue(&[
            parsed(trace_declaration()),
            parsed(parse_quote! {
                #[gas = 350]
                #[wasm_name = "check_id"]
                fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8])
                    -> HostResult<usize>;
            }),
        ]));

        for item in [
            "VmState",
            "CallResult",
            "charged",
            "InBytes",
            "InStr",
            "InU32",
            "OutBytes",
            "TraceCode",
        ] {
            for (index, _) in glue.match_indices(item) {
                assert!(
                    glue[..index].ends_with("$ env :: "),
                    "`{item}` named outside `$env`: {glue}"
                );
            }
        }
    }

    /// The charging helpers' signatures, which nothing else in the contract
    /// states.
    #[test]
    fn pins_both_charging_helpers_signatures() {
        assert_eq!(
            charging_assertions().to_string(),
            "const _ : fn (& mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
             $ crate :: HostFunctionSpec , \
             fn (& mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >>) \
             -> $ env :: CallResult < i32 > ,) \
             -> :: core :: result :: Result < i32 , :: wasmi :: Error > = $ env :: charged ; \
             const _ : fn (& mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >> , \
             $ crate :: HostFunctionSpec , \
             fn (& mut :: wasmi :: Caller < '_ , $ env :: VmState < '_ >>) \
             -> $ env :: CallResult < () > ,) \
             -> :: core :: result :: Result < () , :: wasmi :: Error > \
             = $ env :: charged_unreported ;"
        );
    }

    fn trace_declaration() -> syn::TraitItemFn {
        parse_quote! {
            #[gas = 30]
            #[wasm_name = "trace"]
            fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()>;
        }
    }

    /// The expansion's code alone. `to_string` renders a doc comment as a
    /// `#[doc = "…"]` literal, and the macro's own documentation names the very
    /// items the scans above look for.
    fn code(tokens: TokenStream) -> String {
        fn is_doc(tree: Option<&TokenTree>) -> bool {
            let Some(TokenTree::Group(group)) = tree else {
                return false;
            };
            group.delimiter() == Delimiter::Bracket
                && matches!(group.stream().into_iter().next(),
                            Some(TokenTree::Ident(ident)) if ident == "doc")
        }

        fn strip(tokens: TokenStream) -> TokenStream {
            let mut trees = tokens.into_iter().peekable();
            let mut kept = Vec::new();
            while let Some(tree) = trees.next() {
                match tree {
                    TokenTree::Punct(ref punct)
                        if punct.as_char() == '#' && is_doc(trees.peek()) =>
                    {
                        trees.next();
                    }
                    TokenTree::Group(group) => kept.push(TokenTree::Group(Group::new(
                        group.delimiter(),
                        strip(group.stream()),
                    ))),
                    other => kept.push(other),
                }
            }
            kept.into_iter().collect()
        }

        strip(tokens).to_string()
    }
}
