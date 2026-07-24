use proc_macro::TokenStream;
use quote::quote;
use syn::{
    Attribute, Expr, ExprLit, Lit, Signature, TraitItemFn,
    parse::{Parse, ParseStream},
    parse2,
};

#[proc_macro]
pub fn host_functions(input: TokenStream) -> TokenStream {
    expand(input.into())
        .unwrap_or_else(syn::Error::into_compile_error)
        .into()
}

fn expand(input: proc_macro2::TokenStream) -> syn::Result<proc_macro2::TokenStream> {
    let HostFunctionsInput { functions } = parse2(input)?;

    // let mut errors = Vec::new();

    for f in functions {}

    Ok(quote! {
        trait HostFunctions {

        }

        enum HostFunctionSpec {

        }

        impl HostFunctionSpec
    }
    .into())
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

struct ParsedHostFunction {
    gas: usize,
    wasm_name: String,
    docs: Vec<Attribute>,
    signature: Signature,
}

impl ParsedHostFunction {
    const GAS_PATH: &str = "gas";
    const WASM_NAME_PATH: &str = "wasm_name";

    fn parse(value: TraitItemFn) -> Result<Self, syn::Error> {
        let mut gas = None;
        let mut wasm_name = None;
        let mut docs = Vec::new();
        let mut errors = Vec::new();

        for attr in &value.attrs {
            let named = match attr.meta.require_name_value() {
                Ok(n) => n,
                Err(e) => {
                    errors.push(e);
                    continue;
                }
            };

            match &named.path {
                p if p.is_ident(Self::GAS_PATH) => {
                    let parsed_value = match Self::parse_number(&named.value) {
                        Ok(n) => n,
                        Err(e) => {
                            errors.push(e);
                            continue;
                        }
                    };
                    if gas.replace(parsed_value).is_some() {
                        errors.push(syn::Error::new_spanned(
                            named,
                            format!("duplicated {} attribute", Self::GAS_PATH),
                        ));
                    }
                }
                p if p.is_ident(Self::WASM_NAME_PATH) => {
                    let parsed_value = match Self::parse_string(&named.value) {
                        Ok(n) => n,
                        Err(e) => {
                            errors.push(e);
                            continue;
                        }
                    };
                    if wasm_name.replace(parsed_value).is_some() {
                        errors.push(syn::Error::new_spanned(
                            named,
                            format!("duplicated {} attribute", Self::WASM_NAME_PATH),
                        ));
                    }
                }
                p if p.is_ident("doc") => {
                    docs.push(attr.clone());
                }
                _ => {
                    errors.push(syn::Error::new_spanned(named, "unexpected attribute"));
                }
            }
        }

        if !errors.is_empty() {
            return Err(errors
                .into_iter()
                .reduce(|mut l, r| {
                    l.combine(r);
                    l
                })
                .unwrap());
        }
        if gas.is_none() {
            return Err(syn::Error::new_spanned(
                &value.sig,
                format!("missing {} attribute", Self::GAS_PATH),
            ));
        }

        if wasm_name.is_none() {
            return Err(syn::Error::new_spanned(
                &value.sig,
                format!("missing {} attribute", Self::WASM_NAME_PATH),
            ));
        }

        Ok(Self {
            gas: gas.unwrap(),
            wasm_name: wasm_name.unwrap(),
            docs,
            signature: value.sig,
        })
    }

    fn parse_number(value: &Expr) -> Result<usize, syn::Error> {
        match value {
            Expr::Lit(ExprLit {
                lit: Lit::Int(i), ..
            }) => i.base10_parse::<usize>(),
            other => Err(syn::Error::new_spanned(
                other,
                "expected an integer literal",
            )),
        }
    }

    fn parse_string(value: &Expr) -> Result<String, syn::Error> {
        match value {
            Expr::Lit(ExprLit {
                lit: Lit::Str(s), ..
            }) => Ok(s.value()),
            other => Err(syn::Error::new_spanned(other, "expected string literal")),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_gas_and_wasm_name() {
        let f: TraitItemFn = syn::parse_quote! {
            /// some comment
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        };
        let p = ParsedHostFunction::parse(f).unwrap();
        assert_eq!(p.gas, 60);
        assert_eq!(p.wasm_name, "ldgr_index");
    }

    #[test]
    fn rejects_unknown_attribute() {
        let f: TraitItemFn = syn::parse_quote! {
            #[gas = 60]
            #[wsam_name = "typo"]
            fn get_ledger_sqn() -> [u8; 4];
        };
        assert!(ParsedHostFunction::parse(f).is_err());
    }
}
