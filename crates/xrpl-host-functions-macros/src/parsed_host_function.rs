use syn::{Attribute, Expr, ExprLit, Lit, Signature, TraitItemFn};

use crate::errors;

/// `#[gas = N]`: the base gas charged before the call runs.
const GAS: &str = "gas";
/// `#[wasm_name = "..."]`: the name the guest imports the function under.
const WASM_NAME: &str = "wasm_name";
/// `///` desugars to `#[doc = "..."]` before macro expansion.
const DOC: &str = "doc";

/// One entry of a `host_functions!` block: its ABI metadata and its signature.
pub(crate) struct ParsedHostFunction {
    pub(crate) gas: u64,
    pub(crate) wasm_name: String,
    /// Doc comments, in source order, to re-emit on the generated items.
    pub(crate) docs: Vec<Attribute>,
    pub(crate) signature: Signature,
}

impl ParsedHostFunction {
    pub(crate) fn parse(function: TraitItemFn) -> syn::Result<Self> {
        let mut gas = None;
        let mut wasm_name = None;
        let mut docs = Vec::new();
        let mut errors = Vec::new();

        // Tracked separately from `gas`/`wasm_name` so a malformed attribute is
        // not also reported as a missing one.
        let mut saw_gas = false;
        let mut saw_wasm_name = false;

        for attr in function.attrs {
            if attr.path().is_ident(GAS) {
                saw_gas = true;
                if let Err(error) = int_value(&attr).and_then(|v| set_once(&mut gas, v, &attr)) {
                    errors.push(error);
                }
            } else if attr.path().is_ident(WASM_NAME) {
                saw_wasm_name = true;
                if let Err(error) =
                    string_value(&attr).and_then(|v| set_once(&mut wasm_name, v, &attr))
                {
                    errors.push(error);
                }
            } else if attr.path().is_ident(DOC) {
                docs.push(attr);
            } else {
                errors.push(syn::Error::new_spanned(
                    &attr,
                    format!("unexpected attribute `{}`", path_name(&attr)),
                ));
            }
        }

        if !saw_gas {
            errors.push(syn::Error::new_spanned(
                &function.sig.ident,
                format!("missing `#[{GAS} = ...]` attribute"),
            ));
        }
        if !saw_wasm_name {
            errors.push(syn::Error::new_spanned(
                &function.sig.ident,
                format!("missing `#[{WASM_NAME} = \"...\"]` attribute"),
            ));
        }
        if let Some(body) = &function.default {
            errors.push(syn::Error::new_spanned(
                body,
                "a host function is implemented by the host, so it must not have a body",
            ));
        }
        if !function.sig.generics.params.is_empty() || function.sig.generics.where_clause.is_some()
        {
            errors.push(syn::Error::new_spanned(
                &function.sig.ident,
                "a host function must not be generic: it maps to one wasm import signature",
            ));
        }
        if let Some(receiver) = function.sig.receiver() {
            errors.push(syn::Error::new_spanned(
                receiver,
                "the receiver is added by the macro; declare only the wasm parameters",
            ));
        }

        if let Some(error) = errors::combine(errors) {
            return Err(error);
        }

        let (Some(gas), Some(wasm_name)) = (gas, wasm_name) else {
            unreachable!("absent attributes are reported above");
        };

        Ok(Self {
            gas,
            wasm_name,
            docs,
            signature: function.sig,
        })
    }
}

/// Records `value`, or reports that the attribute appeared more than once.
fn set_once<T>(slot: &mut Option<T>, value: T, attr: &Attribute) -> syn::Result<()> {
    if slot.replace(value).is_some() {
        return Err(syn::Error::new_spanned(
            attr,
            format!("duplicate `{}` attribute", path_name(attr)),
        ));
    }
    Ok(())
}

fn int_value(attr: &Attribute) -> syn::Result<u64> {
    match &attr.meta.require_name_value()?.value {
        Expr::Lit(ExprLit {
            lit: Lit::Int(int), ..
        }) => int.base10_parse(),
        other => Err(syn::Error::new_spanned(
            other,
            format!("`{}` expects an integer literal", path_name(attr)),
        )),
    }
}

fn string_value(attr: &Attribute) -> syn::Result<String> {
    match &attr.meta.require_name_value()?.value {
        Expr::Lit(ExprLit {
            lit: Lit::Str(string),
            ..
        }) => Ok(string.value()),
        other => Err(syn::Error::new_spanned(
            other,
            format!("`{}` expects a string literal", path_name(attr)),
        )),
    }
}

/// The attribute's path as written, for diagnostics: `gas`, or `foo::bar`.
fn path_name(attr: &Attribute) -> String {
    attr.path()
        .segments
        .iter()
        .map(|segment| segment.ident.to_string())
        .collect::<Vec<_>>()
        .join("::")
}

#[cfg(test)]
mod tests {
    use super::*;
    use syn::parse_quote;

    /// The message of every diagnostic recorded by one failed `parse`.
    ///
    /// `expect_err` is unavailable here: it needs `T: Debug`, and syn only
    /// implements `Debug` for its AST types under the `extra-traits` feature.
    fn messages(function: TraitItemFn) -> Vec<String> {
        let Err(error) = ParsedHostFunction::parse(function) else {
            panic!("expected parsing to fail");
        };
        error.into_iter().map(|error| error.to_string()).collect()
    }

    fn doc_text(attr: &Attribute) -> String {
        match &attr.meta.require_name_value().unwrap().value {
            Expr::Lit(ExprLit {
                lit: Lit::Str(text),
                ..
            }) => text.value(),
            _ => panic!("doc attribute is not a string literal"),
        }
    }

    #[test]
    fn reads_gas_and_wasm_name() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        })
        .unwrap();

        assert_eq!(parsed.gas, 60);
        assert_eq!(parsed.wasm_name, "ldgr_index");
        assert_eq!(parsed.signature.ident.to_string(), "get_ledger_sqn");
        assert!(parsed.docs.is_empty());
    }

    #[test]
    fn keeps_doc_comments_in_source_order() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            /// First line.
            ///
            /// Third line.
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        })
        .unwrap();

        let docs: Vec<_> = parsed.docs.iter().map(doc_text).collect();
        assert_eq!(docs, vec![" First line.", "", " Third line."]);
    }

    #[test]
    fn preserves_parameters_and_return_type() {
        let traced = ParsedHostFunction::parse(parse_quote! {
            #[gas = 500]
            #[wasm_name = "trace"]
            fn trace(msg: &str, data: &[u8], as_hex: bool);
        })
        .unwrap();
        assert_eq!(traced.signature.inputs.len(), 3);
        assert!(matches!(traced.signature.output, syn::ReturnType::Default));

        let hashed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 2000]
            #[wasm_name = "sha512_half"]
            fn sha512_half(data: &[u8]) -> [u8; 32];
        })
        .unwrap();
        assert!(matches!(hashed.signature.output, syn::ReturnType::Type(..)));
    }

    #[test]
    fn reports_both_missing_attributes_at_once() {
        let messages = messages(parse_quote! {
            fn get_ledger_sqn() -> [u8; 4];
        });

        assert_eq!(messages.len(), 2);
        assert!(messages[0].contains("missing `#[gas"), "{messages:?}");
        assert!(messages[1].contains("missing `#[wasm_name"), "{messages:?}");
    }

    #[test]
    fn names_the_unexpected_attribute() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wsam_name = "typo"]
            fn get_ledger_sqn() -> [u8; 4];
        });

        // The typo'd attribute, plus the `wasm_name` it failed to be.
        assert_eq!(messages.len(), 2);
        assert!(
            messages.iter().any(|m| m.contains("`wsam_name`")),
            "{messages:?}"
        );
    }

    #[test]
    fn rejects_wrong_literal_types() {
        let gas = messages(parse_quote! {
            #[gas = "60"]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        });
        assert_eq!(gas.len(), 1, "{gas:?}");
        assert!(
            gas[0].contains("`gas` expects an integer literal"),
            "{gas:?}"
        );

        let name = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = 7]
            fn get_ledger_sqn() -> [u8; 4];
        });
        assert_eq!(name.len(), 1, "{name:?}");
        assert!(
            name[0].contains("`wasm_name` expects a string literal"),
            "{name:?}"
        );
    }

    #[test]
    fn rejects_gas_that_does_not_fit_in_u64() {
        let messages = messages(parse_quote! {
            #[gas = 99999999999999999999999]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("number too large"), "{messages:?}");
    }

    #[test]
    fn rejects_attribute_shapes_other_than_name_value() {
        let bare = messages(parse_quote! {
            #[gas]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        });
        assert_eq!(bare.len(), 1, "{bare:?}");
        assert!(bare[0].contains("gas = ..."), "{bare:?}");

        let list = messages(parse_quote! {
            #[gas(60)]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        });
        assert_eq!(list.len(), 1, "{list:?}");
    }

    #[test]
    fn rejects_duplicate_attributes() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[gas = 70]
            #[wasm_name = "ldgr_index"]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4];
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
        assert!(messages[0].contains("duplicate `gas`"), "{messages:?}");
        assert!(
            messages[1].contains("duplicate `wasm_name`"),
            "{messages:?}"
        );
    }

    /// A malformed attribute must not also be reported as an absent one.
    #[test]
    fn does_not_report_a_malformed_attribute_as_missing() {
        let messages = messages(parse_quote! {
            #[gas = "60"]
            #[wasm_name = 7]
            fn get_ledger_sqn() -> [u8; 4];
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
        assert!(
            !messages.iter().any(|m| m.contains("missing")),
            "{messages:?}"
        );
    }

    #[test]
    fn rejects_a_body() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4] { [0; 4] }
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("must not have a body"), "{messages:?}");
    }

    #[test]
    fn rejects_generics() {
        let parameter = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn<T>() -> T;
        });
        assert_eq!(parameter.len(), 1, "{parameter:?}");
        assert!(
            parameter[0].contains("must not be generic"),
            "{parameter:?}"
        );

        let clause = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> [u8; 4] where Self: Sized;
        });
        assert_eq!(clause.len(), 1, "{clause:?}");
    }

    #[test]
    fn rejects_an_explicit_receiver() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> [u8; 4];
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("receiver"), "{messages:?}");
    }
}
