mod errors;
mod parsed_host_function;

use std::collections::HashSet;

use proc_macro2::TokenStream;
use quote::quote;
use syn::{
    TraitItemFn,
    parse::{Parse, ParseStream},
    parse2,
};

use parsed_host_function::ParsedHostFunction;

/// Declares the wasm host ABI once, and generates everything that follows from it.
///
/// The input is a block of `fn` declarations, each carrying the gas cost the host
/// charges before the call and the name the guest imports it under. Doc comments
/// are kept and appear on the generated items.
///
/// This crate is an implementation detail of `xrpl-host-functions`, which
/// hand-writes the types the expansion refers to and holds the one declaration
/// block. The expansion names those types by absolute path, so a call site needs
/// `xrpl-host-functions` as a dependency but no imports from it.
///
/// ```
/// use xrpl_host_functions_macros::host_functions;
///
/// host_functions! {
///     /// The sequence number of the ledger being built.
///     #[gas = 60]
///     #[wasm_name = "ldgr_index"]
///     fn get_ledger_sqn(&self) -> [u8; 4];
///
///     /// Writes `msg` to the trace log.
///     #[gas = 500]
///     #[wasm_name = "trace_num"]
///     fn trace_num(&self, msg: &str, number: i64);
/// }
///
/// // A `HostFunctions` trait, holding the declarations verbatim:
/// struct Host;
/// impl HostFunctions for Host {
///     fn get_ledger_sqn(&self) -> [u8; 4] { 7u32.to_le_bytes() }
///     fn trace_num(&self, _msg: &str, _number: i64) {}
/// }
///
/// // A `HostFunctionSpec` enum carrying the ABI metadata as a `const` table:
/// assert_eq!(HostFunctionSpec::GetLedgerSqn.gas(), 60);
/// assert_eq!(HostFunctionSpec::TraceNum.wasm_name(), "trace_num");
/// assert_eq!(HostFunctionSpec::ALL.len(), 2);
/// ```
///
/// A declaration must be a plain `fn` taking `&self`, with no body and no
/// generics: it maps to exactly one wasm import signature. Two declarations may
/// not share a `wasm_name`, nor collapse to the same PascalCase variant.
#[proc_macro]
pub fn host_functions(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    expand(input.into())
        .unwrap_or_else(syn::Error::into_compile_error)
        .into()
}

fn expand(input: TokenStream) -> syn::Result<TokenStream> {
    let HostFunctionsInput { functions } = parse2(input)?;

    let mut parsed = Vec::with_capacity(functions.len());
    let mut errors = Vec::new();
    for function in functions {
        match ParsedHostFunction::parse(function) {
            Ok(function) => parsed.push(function),
            Err(error) => errors.push(error),
        }
    }
    if let Some(error) = errors::combine(errors) {
        return Err(error);
    }
    if let Some(error) = errors::combine(collisions(&parsed)) {
        return Err(error);
    }

    Ok(generate(&parsed))
}

/// Names two declarations may not share, because the generated code would then
/// fail to compile at a span the caller cannot see.
fn collisions(functions: &[ParsedHostFunction]) -> Vec<syn::Error> {
    let mut errors = Vec::new();
    let mut variants = HashSet::new();
    let mut wasm_names = HashSet::new();

    for function in functions {
        if !variants.insert(function.variant.to_string()) {
            errors.push(syn::Error::new_spanned(
                &function.variant,
                format!(
                    "another host function already becomes the `{}` variant",
                    function.variant
                ),
            ));
        }
        if !wasm_names.insert(function.wasm_name.value()) {
            errors.push(syn::Error::new_spanned(
                &function.wasm_name,
                format!(
                    "another host function is already imported as `{}`",
                    function.wasm_name.value()
                ),
            ));
        }
    }

    errors
}

/// Path to the hand-written `HostFnSpec` the expansion refers to.
///
/// Absolute, so the generated code resolves whatever the caller has imported and
/// whatever else is named `HostFnSpec` in scope. `xrpl-host-functions` declares
/// `extern crate self as xrpl_host_functions;`, which is what lets this path
/// resolve inside the crate the ABI is declared in.
pub(crate) fn host_fn_spec_path() -> TokenStream {
    quote! { ::xrpl_host_functions::HostFnSpec }
}

fn generate(functions: &[ParsedHostFunction]) -> TokenStream {
    let trait_methods = functions.iter().map(ParsedHostFunction::trait_method);
    let variants = functions
        .iter()
        .map(ParsedHostFunction::variant_declaration);
    let spec_arms = functions.iter().map(ParsedHostFunction::spec_arm);
    let all = functions.iter().map(|function| &function.variant);
    let spec_type = host_fn_spec_path();

    quote! {
        /// The host side of the wasm ABI: one method per function a guest may
        /// import.
        ///
        /// Implement it once per execution environment — the ledger host, a test
        /// double, a benchmark fake — and a guest module cannot tell them apart.
        /// Each method is one declaration from the `host_functions!` block, as
        /// written; its `&self` receiver is not part of the ABI the guest sees,
        /// so a host that must mutate does so behind interior mutability.
        pub trait HostFunctions {
            #(#trait_methods)*
        }

        /// Identifies one host function, and is the compile-time source of its
        /// ABI metadata.
        ///
        /// One variant per `host_functions!` declaration, named by converting the
        /// function name to PascalCase. [`Self::ALL`] is the whole ABI, which is
        /// what a wasm engine iterates to build its import table.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum HostFunctionSpec {
            #(#variants,)*
        }

        impl HostFunctionSpec {
            /// Every host function, in the order declared.
            ///
            /// This is the complete import surface a guest may link against: a
            /// function absent here cannot be called, and one present here must
            /// be registered for a module that imports it to instantiate.
            pub const ALL: &'static [Self] = &[#(Self::#all,)*];

            /// This function's import name and base gas cost.
            ///
            /// Usable in `const` context, so gas tables and import lists can be
            /// built at compile time.
            pub const fn spec(self) -> #spec_type {
                match self {
                    #(#spec_arms,)*
                }
            }

            /// The name a guest imports this function under.
            ///
            /// A guest's import name must match this exactly, or the module
            /// fails to instantiate.
            pub const fn wasm_name(self) -> &'static str {
                self.spec().name
            }

            /// Gas charged before the call runs, independent of its arguments.
            ///
            /// Consensus-relevant: two nodes that disagree on this value
            /// disagree on transaction outcomes.
            pub const fn gas(self) -> u64 {
                self.spec().gas
            }
        }
    }
}

struct HostFunctionsInput {
    functions: Vec<TraitItemFn>,
}

impl Parse for HostFunctionsInput {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let mut functions = Vec::new();
        while !input.is_empty() {
            functions.push(input.parse()?);
        }
        Ok(HostFunctionsInput { functions })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_an_empty_block() {
        expand(quote! {}).unwrap();
    }

    #[test]
    fn reports_mistakes_from_every_function() {
        let error = expand(quote! {
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> [u8; 4];

            #[gas = 2000]
            fn sha512_half(&self, data: &[u8]) -> [u8; 32];
        })
        .expect_err("expected parsing to fail");

        let messages: Vec<_> = error.into_iter().map(|error| error.to_string()).collect();
        assert_eq!(messages.len(), 2, "{messages:?}");
        assert!(messages[0].contains("missing `#[gas"), "{messages:?}");
        assert!(messages[1].contains("missing `#[wasm_name"), "{messages:?}");
    }

    #[test]
    fn propagates_syntax_errors() {
        let error = expand(quote! { fn missing_semicolon() }).expect_err("expected a syntax error");
        assert!(!error.to_string().is_empty());
    }

    /// The messages of every diagnostic recorded by one failed `expand`.
    fn messages(input: TokenStream) -> Vec<String> {
        let Err(error) = expand(input) else {
            panic!("expected expansion to fail");
        };
        error.into_iter().map(|error| error.to_string()).collect()
    }

    #[test]
    fn generates_the_trait_the_enum_and_the_table() {
        let generated = expand(quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> [u8; 4];

            #[gas = 500]
            #[wasm_name = "trace_num"]
            fn trace_num(&self, msg: &str, number: i64);
        })
        .unwrap()
        .to_string();

        for expected in [
            "pub trait HostFunctions",
            "fn get_ledger_sqn (& self) -> [u8 ; 4] ;",
            "fn trace_num (& self , msg : & str , number : i64) ;",
            "pub enum HostFunctionSpec { GetLedgerSqn , TraceNum , }",
            "pub const ALL : & 'static [Self] = & [Self :: GetLedgerSqn , Self :: TraceNum ,]",
            "pub const fn spec (self) -> :: xrpl_host_functions :: HostFnSpec",
            "Self :: GetLedgerSqn => :: xrpl_host_functions :: HostFnSpec \
             { name : \"ldgr_index\" , gas : 60u64 }",
        ] {
            assert!(generated.contains(expected), "missing {expected:?}");
        }
    }

    /// The expansion names the types it needs by absolute path, so it cannot pick
    /// up a different `HostFnSpec` that happens to be in scope where it lands.
    #[test]
    fn refers_to_the_declaring_crate_by_absolute_path() {
        let generated = expand(quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> [u8; 4];
        })
        .unwrap()
        .to_string();

        assert_eq!(generated.matches("HostFnSpec").count(), 2, "{generated}");
        assert_eq!(
            generated
                .matches(":: xrpl_host_functions :: HostFnSpec")
                .count(),
            2,
            "{generated}"
        );
    }

    #[test]
    fn rejects_two_functions_that_share_a_wasm_name() {
        let messages = messages(quote! {
            #[gas = 60]
            #[wasm_name = "trace"]
            fn trace(&self, msg: &str);

            #[gas = 70]
            #[wasm_name = "trace"]
            fn trace_num(&self, msg: &str, number: i64);
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("already imported as `trace`"),
            "{messages:?}"
        );
    }

    /// Names that differ only in underscores collapse to one enum variant.
    #[test]
    fn rejects_two_functions_that_share_a_variant() {
        let messages = messages(quote! {
            #[gas = 60]
            #[wasm_name = "a"]
            fn get_ledger_sqn(&self) -> [u8; 4];

            #[gas = 70]
            #[wasm_name = "b"]
            fn get_ledger__sqn(&self) -> [u8; 4];
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("`GetLedgerSqn` variant"),
            "{messages:?}"
        );
    }
}
