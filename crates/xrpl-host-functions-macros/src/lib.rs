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

fn generate(functions: &[ParsedHostFunction]) -> TokenStream {
    let trait_methods = functions.iter().map(ParsedHostFunction::trait_method);
    let variants = functions
        .iter()
        .map(ParsedHostFunction::variant_declaration);
    let spec_arms = functions.iter().map(ParsedHostFunction::spec_arm);
    let all = functions.iter().map(|function| &function.variant);

    quote! {
        /// The host ABI: one method per function a guest may import.
        pub trait HostFunctions {
            #(#trait_methods)*
        }

        /// Identifies a host function, and carries its ABI metadata.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum HostFunctionSpec {
            #(#variants,)*
        }

        impl HostFunctionSpec {
            /// Every host function, in declaration order.
            pub const ALL: &'static [Self] = &[#(Self::#all,)*];

            /// The wasm import name and base gas cost of this function.
            pub const fn spec(self) -> HostFnSpec {
                match self {
                    #(#spec_arms,)*
                }
            }

            /// The name a guest imports this function under.
            pub const fn wasm_name(self) -> &'static str {
                self.spec().name
            }

            /// The consensus-fixed base gas charged before the call runs.
            pub const fn gas(self) -> u64 {
                self.spec().base_gas
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
            fn get_ledger_sqn() -> [u8; 4];

            #[gas = 2000]
            fn sha512_half(data: &[u8]) -> [u8; 32];
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
            fn get_ledger_sqn() -> [u8; 4];

            #[gas = 500]
            #[wasm_name = "trace_num"]
            fn trace_num(msg: &str, number: i64);
        })
        .unwrap()
        .to_string();

        for expected in [
            "pub trait HostFunctions",
            "fn get_ledger_sqn (& mut self) -> [u8 ; 4] ;",
            "fn trace_num (& mut self , msg : & str , number : i64) ;",
            "pub enum HostFunctionSpec { GetLedgerSqn , TraceNum , }",
            "pub const ALL : & 'static [Self] = & [Self :: GetLedgerSqn , Self :: TraceNum ,]",
            "pub const fn spec (self) -> HostFnSpec",
            "Self :: GetLedgerSqn => HostFnSpec { name : \"ldgr_index\" , base_gas : 60u64 }",
        ] {
            assert!(generated.contains(expected), "missing {expected:?}");
        }
    }

    #[test]
    fn rejects_two_functions_that_share_a_wasm_name() {
        let messages = messages(quote! {
            #[gas = 60]
            #[wasm_name = "trace"]
            fn trace(msg: &str);

            #[gas = 70]
            #[wasm_name = "trace"]
            fn trace_num(msg: &str, number: i64);
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
            fn get_ledger_sqn() -> [u8; 4];

            #[gas = 70]
            #[wasm_name = "b"]
            fn get_ledger__sqn() -> [u8; 4];
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("`GetLedgerSqn` variant"),
            "{messages:?}"
        );
    }
}
