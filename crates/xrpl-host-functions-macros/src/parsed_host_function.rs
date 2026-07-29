use proc_macro2::TokenStream;
use quote::{format_ident, quote};
use syn::{
    Attribute, Expr, ExprLit, Ident, Lit, LitStr, PathArguments, ReceiverKind, ReturnType, Safety,
    Signature, TraitItemFn, Type, TypePath,
};

use crate::errors;

/// `#[gas = N]`: the base gas charged before the call runs.
const GAS: &str = "gas";
/// `#[wasm_name = "..."]`: the name the guest imports the function under.
const WASM_NAME: &str = "wasm_name";
/// `///` desugars to `#[doc = "..."]` before macro expansion.
const DOC: &str = "doc";
/// The alias every declaration returns its success type through.
const HOST_RESULT: &str = "HostResult";

/// One entry of a `host_functions!` block: its ABI metadata and its signature.
pub(crate) struct ParsedHostFunction {
    pub(crate) gas: u64,
    /// Kept as the literal the user wrote, so diagnostics and the generated
    /// string both carry that span.
    pub(crate) wasm_name: LitStr,
    /// Doc comments, in source order, to re-emit on the generated items.
    pub(crate) docs: Vec<Attribute>,
    /// The enum variant this declaration becomes, spanned at the function name.
    pub(crate) variant: Ident,
    pub(crate) signature: Signature,
}

impl ParsedHostFunction {
    /// `#[doc …] fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;`
    pub(crate) fn trait_method(&self) -> TokenStream {
        let docs = &self.docs;
        // The declaration is already a trait method: emitted verbatim, so what
        // the block reads like is what the trait is.
        let signature = &self.signature;

        quote! {
            #(#docs)*
            #signature;
        }
    }

    /// `#[doc …] GetLedgerSqn`
    pub(crate) fn variant_declaration(&self) -> TokenStream {
        let docs = &self.docs;
        let variant = &self.variant;
        quote! {
            #(#docs)*
            #variant
        }
    }

    /// `Self::GetLedgerSqn => HostFnSpec { name: "ldgr_index", gas: 60u64 }`
    pub(crate) fn spec_arm(&self) -> TokenStream {
        let Self {
            gas,
            wasm_name,
            variant,
            ..
        } = self;
        quote! {
            Self::#variant => HostFnSpec { name: #wasm_name, gas: #gas }
        }
    }

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
        errors.extend(check_receiver(&function.sig).err());
        errors.extend(check_return_type(&function.sig).err());
        if let Some(name) = &wasm_name {
            errors.extend(check_wasm_name(name).err());
        }
        reject_modifiers(&function.sig, &mut errors);

        // A name whose PascalCase form is not a legal variant is reported here
        // rather than emitted, which would either panic or fail downstream.
        let variant = match variant_ident(&function.sig.ident) {
            Ok(variant) => Some(variant),
            Err(error) => {
                errors.push(error);
                None
            }
        };

        if let Some(error) = errors::combine(errors) {
            return Err(error);
        }

        let (Some(gas), Some(wasm_name), Some(variant)) = (gas, wasm_name, variant) else {
            unreachable!("every absent field is reported above");
        };

        Ok(Self {
            gas,
            wasm_name,
            docs,
            variant,
            signature: function.sig,
        })
    }
}

/// Every declaration carries a receiver, and it is always `&self`.
///
/// `&self` is the only receiver that can work: the VM reaches the host through a
/// shared `&dyn HostFunctions` stored in the wasmi `Store`, and a host that needs
/// to mutate does so behind interior mutability. The receiver is not part of the
/// wasm ABI — the guest passes no `self` — so it is uniform across the block.
fn check_receiver(signature: &Signature) -> syn::Result<()> {
    let Some(receiver) = signature.receiver() else {
        return Err(syn::Error::new_spanned(
            &signature.ident,
            format!(
                "a host function must declare its receiver: `fn {}(&self, ...)`",
                signature.ident
            ),
        ));
    };

    // `&self` and nothing else: not `&mut self`, not `self`/`mut self`, not a
    // typed `self: Box<Self>`, and not a spelled-out lifetime.
    if !matches!(receiver.kind, ReceiverKind::Reference(_, None, None)) {
        return Err(syn::Error::new_spanned(
            receiver,
            "a host function's receiver must be exactly `&self`: the VM calls the host \
             through a shared `&dyn HostFunctions`",
        ));
    }
    Ok(())
}

/// Every declaration returns `HostResult<T>`, including the ones that yield
/// nothing (`HostResult<()>`).
///
/// One shape for every function is what lets a single dispatch adapter lower them
/// all: lift the arguments out of guest memory, call the host, then turn `Ok(T)`
/// into the wire's non-negative `i32` and `Err(e)` into a negative code or a trap.
/// A function returning a bare `T` would need its own arm.
fn check_return_type(signature: &Signature) -> syn::Result<()> {
    const SHAPE: &str = "a host function must return `HostResult<T>` — \
                         `HostResult<()>` if it yields nothing";

    let ReturnType::Type(_, returned) = &signature.output else {
        return Err(syn::Error::new_spanned(&signature.ident, SHAPE));
    };

    let Type::Path(TypePath {
        qself: None, path, ..
    }) = &**returned
    else {
        return Err(syn::Error::new_spanned(returned, SHAPE));
    };
    // The last segment only, so `HostResult<T>` may be written qualified.
    let Some(last) = path.segments.last() else {
        return Err(syn::Error::new_spanned(returned, SHAPE));
    };
    if last.ident != HOST_RESULT {
        return Err(syn::Error::new_spanned(returned, SHAPE));
    }

    // `HostResult` without its success type is `HostResult` the alias, which names
    // no type; rustc's own message for that is unhelpfully far from the cause.
    let PathArguments::AngleBracketed(arguments) = &last.arguments else {
        return Err(syn::Error::new_spanned(
            returned,
            format!("`{HOST_RESULT}` needs its success type: `{HOST_RESULT}<T>`"),
        ));
    };
    if arguments.args.len() != 1 {
        return Err(syn::Error::new_spanned(
            arguments,
            format!("`{HOST_RESULT}` takes exactly one type: `{HOST_RESULT}<T>`"),
        ));
    }
    Ok(())
}

/// `const`, `async`, `unsafe`/`safe` and `extern "…"` have no meaning in the
/// wasm ABI, and would otherwise pass silently into the generated trait.
fn reject_modifiers(signature: &Signature, errors: &mut Vec<syn::Error>) {
    const PLAIN: &str =
        "a host function must be a plain `fn`: this modifier is not part of the wasm ABI";

    if let Some(constness) = &signature.constness {
        errors.push(syn::Error::new_spanned(constness, PLAIN));
    }
    if let Some(asyncness) = &signature.asyncness {
        errors.push(syn::Error::new_spanned(asyncness, PLAIN));
    }
    match &signature.safety {
        Safety::Default => {}
        Safety::Safe(token) => errors.push(syn::Error::new_spanned(token, PLAIN)),
        Safety::Unsafe(token) => errors.push(syn::Error::new_spanned(token, PLAIN)),
    }
    if let Some(abi) = &signature.abi {
        errors.push(syn::Error::new_spanned(abi, PLAIN));
    }
}

/// The wasm import name reaches the engine's import table verbatim, so it is
/// held to what an import name can sanely be rather than to any string.
fn check_wasm_name(name: &LitStr) -> syn::Result<()> {
    let value = name.value();
    if value.is_empty() {
        return Err(syn::Error::new_spanned(
            name,
            "the wasm name must not be empty",
        ));
    }
    if let Some(character) = value
        .chars()
        .find(|c| !c.is_ascii_alphanumeric() && *c != '_')
    {
        return Err(syn::Error::new_spanned(
            name,
            format!(
                "a wasm name may only contain `A-Za-z0-9_`, but this one contains {character:?}"
            ),
        ));
    }
    Ok(())
}

/// The enum variant a declaration becomes: `get_ledger_sqn` -> `GetLedgerSqn`.
///
/// The result carries `ident`'s span, so anything the compiler says about the
/// variant points at the declaration that produced it.
fn variant_ident(ident: &Ident) -> syn::Result<Ident> {
    // `to_string` spells raw identifiers `r#type`; the `r#` is not part of the name.
    let name = ident.to_string();
    let name = name.strip_prefix("r#").unwrap_or(&name);

    let mut pascal = String::with_capacity(name.len());
    let mut capitalize = true;
    for character in name.chars() {
        if character == '_' {
            capitalize = true;
        } else if capitalize {
            pascal.extend(character.to_uppercase());
            capitalize = false;
        } else {
            pascal.push(character);
        }
    }

    // A name of nothing but underscores leaves `pascal` empty; the original is
    // already a legal identifier, so keep it.
    if pascal.is_empty() {
        return Ok(ident.clone());
    }

    // `Ident::new` panics on a leading digit (`_2fa` -> `2fa`) and silently
    // accepts keyword spellings (`self_` -> `Self`), which then fails to parse
    // where the variant is emitted. Parsing rejects both, without panicking.
    if let Err(error) = syn::parse_str::<Ident>(&pascal) {
        return Err(syn::Error::new_spanned(
            ident,
            format!(
                "this name becomes the enum variant `{pascal}`, which is not a valid \
                 variant name ({error}); rename the host function"
            ),
        ));
    }
    Ok(format_ident!("{pascal}", span = ident.span()))
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
        }) => {
            // `LitInt` keeps the sign in its digits, so `base10_parse::<u64>`
            // would report a negative value as "invalid digit found in string".
            if int.base10_digits().starts_with('-') {
                return Err(syn::Error::new_spanned(
                    int,
                    format!("`{}` must not be negative", path_name(attr)),
                ));
            }
            int.base10_parse()
        }
        other => Err(syn::Error::new_spanned(
            other,
            format!("`{}` expects an integer literal", path_name(attr)),
        )),
    }
}

fn string_value(attr: &Attribute) -> syn::Result<LitStr> {
    match &attr.meta.require_name_value()?.value {
        Expr::Lit(ExprLit {
            lit: Lit::Str(string),
            ..
        }) => Ok(string.clone()),
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
    use quote::ToTokens;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        })
        .unwrap();

        assert_eq!(parsed.gas, 60);
        assert_eq!(parsed.wasm_name.value(), "ldgr_index");
        assert_eq!(parsed.signature.ident.to_string(), "get_ledger_sqn");
        assert_eq!(parsed.variant.to_string(), "GetLedgerSqn");
        assert!(parsed.docs.is_empty());
    }

    #[test]
    fn derives_variant_names_from_function_names() {
        for (function, variant) in [
            ("get_ledger_sqn", "GetLedgerSqn"),
            ("sha512_half", "Sha512Half"),
            ("trace", "Trace"),
            ("get_current_ledger_obj_field", "GetCurrentLedgerObjField"),
            ("r#type", "Type"),
            ("trace2", "Trace2"),
            // Pathological, but must not panic: no letters to capitalize.
            ("__", "__"),
        ] {
            let ident = format_ident!("{function}");
            assert_eq!(
                variant_ident(&ident).map(|v| v.to_string()).ok(),
                Some(variant.to_owned()),
                "{function}"
            );
        }
    }

    /// `_2fa` would PascalCase to `2fa`; building that `Ident` panics, and a
    /// panic in a proc macro is reported with no useful span at all.
    #[test]
    fn rejects_a_name_that_becomes_a_leading_digit() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "two_factor"]
            fn _2fa(&self) -> HostResult<()>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("becomes the enum variant `2fa`"),
            "{messages:?}"
        );
    }

    /// `self_` PascalCases to `Self`, which `Ident::new` accepts and rustc then
    /// rejects where the variant is emitted. `r#Self` is not a legal escape.
    #[test]
    fn rejects_a_name_that_becomes_a_keyword() {
        for function in ["self_", "_self"] {
            let ident = format_ident!("{function}");
            let Err(error) = variant_ident(&ident) else {
                panic!("expected `{function}` to be rejected");
            };
            assert!(
                error.to_string().contains("variant `Self`"),
                "{}",
                error.to_string()
            );
        }
    }

    #[test]
    fn rejects_negative_gas() {
        let messages = messages(parse_quote! {
            #[gas = -5]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert_eq!(messages[0], "`gas` must not be negative");
    }

    #[test]
    fn rejects_unusable_wasm_names() {
        let empty = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = ""]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });
        assert_eq!(empty.len(), 1, "{empty:?}");
        assert_eq!(empty[0], "the wasm name must not be empty");

        let spaced = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });
        assert_eq!(spaced.len(), 1, "{spaced:?}");
        assert!(spaced[0].contains("may only contain"), "{spaced:?}");
    }

    #[test]
    fn rejects_signature_modifiers() {
        for declaration in [
            quote! { unsafe fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>; },
            quote! { async fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>; },
            quote! { const fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>; },
            quote! { extern "C" fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>; },
        ] {
            let function: TraitItemFn = syn::parse2(quote! {
                #[gas = 60]
                #[wasm_name = "ldgr_index"]
                #declaration
            })
            .unwrap();

            let messages = messages(function);
            assert_eq!(messages.len(), 1, "{messages:?}");
            assert!(messages[0].contains("must be a plain `fn`"), "{messages:?}");
        }
    }

    #[test]
    fn trait_method_keeps_the_declared_receiver_and_ends_in_a_semicolon() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            /// Hashes `data`.
            #[gas = 2000]
            #[wasm_name = "sha512_half"]
            fn sha512_half(&self, data: &[u8]) -> HostResult<[u8; 32]>;
        })
        .unwrap();

        // `///` reaches the macro as `#[doc = r"..."]`: rustc's lexer spells doc
        // comments as raw string literals.
        let method = parsed.trait_method().to_string();
        assert!(
            method.starts_with("# [doc = r\" Hashes `data`.\"]"),
            "{method}"
        );
        assert!(
            method
                .contains("fn sha512_half (& self , data : & [u8]) -> HostResult < [u8 ; 32] > ;"),
            "{method}"
        );
    }

    #[test]
    fn spec_arm_carries_the_name_and_the_gas() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        })
        .unwrap();

        assert_eq!(
            parsed.spec_arm().to_string(),
            "Self :: GetLedgerSqn => HostFnSpec { name : \"ldgr_index\" , gas : 60u64 }"
        );
    }

    #[test]
    fn keeps_doc_comments_in_source_order() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            /// First line.
            ///
            /// Third line.
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()>;
        })
        .unwrap();
        // The receiver is `inputs[0]`; the three wasm parameters follow it.
        assert_eq!(traced.signature.inputs.len(), 4);
        assert_eq!(
            traced.signature.output.to_token_stream().to_string(),
            "-> HostResult < () >"
        );

        let hashed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 2000]
            #[wasm_name = "sha512_half"]
            fn sha512_half(&self, data: &[u8]) -> HostResult<[u8; HASH_LEN]>;
        })
        .unwrap();
        assert_eq!(
            hashed.signature.output.to_token_stream().to_string(),
            "-> HostResult < [u8 ; HASH_LEN] >"
        );
    }

    #[test]
    fn reports_both_missing_attributes_at_once() {
        let messages = messages(parse_quote! {
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });
        assert_eq!(gas.len(), 1, "{gas:?}");
        assert!(
            gas[0].contains("`gas` expects an integer literal"),
            "{gas:?}"
        );

        let name = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = 7]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("number too large"), "{messages:?}");
    }

    #[test]
    fn rejects_attribute_shapes_other_than_name_value() {
        let bare = messages(parse_quote! {
            #[gas]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
        });
        assert_eq!(bare.len(), 1, "{bare:?}");
        assert!(bare[0].contains("gas = ..."), "{bare:?}");

        let list = messages(parse_quote! {
            #[gas(60)]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]>;
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
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]> { Ok([0; 4]) }
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("must not have a body"), "{messages:?}");
    }

    #[test]
    fn rejects_generics() {
        let parameter = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn<T>(&self) -> HostResult<T>;
        });
        assert_eq!(parameter.len(), 1, "{parameter:?}");
        assert!(
            parameter[0].contains("must not be generic"),
            "{parameter:?}"
        );

        let clause = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<[u8; 4]> where Self: Sized;
        });
        assert_eq!(clause.len(), 1, "{clause:?}");
    }

    #[test]
    fn requires_a_receiver() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn() -> HostResult<[u8; 4]>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("must declare its receiver: `fn get_ledger_sqn(&self, ...)`"),
            "{messages:?}"
        );
    }

    /// Anything but `&self` would need a host the VM cannot hand out: it holds
    /// one shared `&dyn HostFunctions` for the whole run.
    #[test]
    fn rejects_receivers_other_than_shared_self() {
        for receiver in [
            quote! { &mut self },
            quote! { self },
            quote! { mut self },
            quote! { self: Box<Self> },
            quote! { &'a self },
        ] {
            let function: TraitItemFn = syn::parse2(quote! {
                #[gas = 60]
                #[wasm_name = "ldgr_index"]
                fn get_ledger_sqn(#receiver) -> HostResult<[u8; 4]>;
            })
            .unwrap_or_else(|_| panic!("`{receiver}` should parse"));

            let messages = messages(function);
            assert_eq!(messages.len(), 1, "`{receiver}`: {messages:?}");
            assert!(
                messages[0].contains("must be exactly `&self`"),
                "`{receiver}`: {messages:?}"
            );
        }
    }

    /// A bare `T` return would need its own lowering arm, so the uniform shape is
    /// required rather than inferred.
    #[test]
    fn rejects_returns_that_are_not_host_result() {
        for output in [
            quote! {},
            quote! { -> () },
            quote! { -> [u8; 4] },
            quote! { -> i32 },
            quote! { -> Result<[u8; 4], HostError> },
            quote! { -> impl Iterator<Item = u8> },
        ] {
            let function: TraitItemFn = syn::parse2(quote! {
                #[gas = 60]
                #[wasm_name = "ldgr_index"]
                fn get_ledger_sqn(&self) #output;
            })
            .unwrap_or_else(|_| panic!("`{output}` should parse"));

            let messages = messages(function);
            assert_eq!(messages.len(), 1, "`{output}`: {messages:?}");
            assert!(
                messages[0].contains("must return `HostResult<T>`"),
                "`{output}`: {messages:?}"
            );
        }
    }

    /// `HostResult` may be written qualified, since the trait method keeps whatever
    /// path resolves where the block is written.
    #[test]
    fn accepts_a_qualified_host_result() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> xrpl_host_functions::HostResult<[u8; 4]>;
        })
        .unwrap();

        assert!(
            parsed
                .trait_method()
                .to_string()
                .contains("xrpl_host_functions :: HostResult < [u8 ; 4] >"),
            "{}",
            parsed.trait_method()
        );
    }

    /// `HostResult` with no success type names no type at all; rustc's own error
    /// for that lands on the generated trait, far from the declaration.
    #[test]
    fn rejects_host_result_without_a_success_type() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("needs its success type"),
            "{messages:?}"
        );
    }
}
