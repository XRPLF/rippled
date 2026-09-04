use proc_macro2::TokenStream;
use quote::{ToTokens, format_ident, quote};
use syn::{
    Attribute, FnArg, GenericArgument, Ident, LitInt, LitStr, Pat, PatIdent, PatType,
    PathArguments, ReceiverKind, ReturnType, Safety, Signature, TraitItemFn, Type, TypePath,
    parse::Parse,
};

use crate::errors;
use crate::lowering::{ParamType, ResultType, WasmValType};

/// `#[gas = N]`: the base gas charged before the call runs.
const GAS: &str = "gas";
/// `#[wasm_name = "..."]`: the name the guest imports the function under.
const WASM_NAME: &str = "wasm_name";
/// `///` desugars to `#[doc = "..."]` before macro expansion.
const DOC: &str = "doc";
/// The alias every declaration returns its success type through.
const HOST_RESULT: &str = "HostResult";

/// One declared parameter: the name it states its purpose with, and the type
/// that decides its wire form.
///
/// The name is carried because the generated glue spells it — as the body's
/// parameter, and as `{name}_ptr`/`{name}_len` for a region.
pub(crate) struct Param {
    pub(crate) name: Ident,
    pub(crate) ty: ParamType,
}

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
    /// The declared parameters, in order, with the receiver dropped: `&self` is
    /// not part of the wasm ABI.
    params: Vec<Param>,
    /// What the declaration's `HostResult<T>` answers with.
    result: ResultType,
}

impl ParsedHostFunction {
    /// The declared parameters, in declaration order, which is wire order.
    pub(crate) fn params(&self) -> &[Param] {
        &self.params
    }

    pub(crate) fn result(&self) -> ResultType {
        self.result
    }

    /// `#[doc …] fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;`
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

    /// `Self::GetLedgerSqn => HostFnSpec { name: "ldgr_index", gas: 60u64,
    /// wasm_params: &[WasmValType::I32, WasmValType::I32], wasm_result:
    /// Some(WasmValType::I32) }`
    pub(crate) fn spec_arm(&self) -> TokenStream {
        let Self {
            gas,
            wasm_name,
            variant,
            result,
            ..
        } = self;
        let wasm_params = self.wasm_params();
        let wasm_result = result.wasm_result_tokens();

        quote! {
            Self::#variant => HostFnSpec {
                name: #wasm_name,
                gas: #gas,
                wasm_params: &[#(#wasm_params),*],
                wasm_result: #wasm_result,
            }
        }
    }

    /// The wasm parameters this declaration lowers to, in wire order and flat:
    /// nothing in the sequence says which two came from one declared region.
    fn wasm_params(&self) -> impl Iterator<Item = WasmValType> {
        self.params
            .iter()
            .flat_map(|param| param.ty.as_wasm_params())
            .copied()
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
                if let Err(error) = value::<LitStr>(&attr, "a string literal")
                    .and_then(|v| set_once(&mut wasm_name, v, &attr))
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
        if let Some(name) = &wasm_name {
            errors.extend(check_wasm_name(name).err());
        }
        errors.extend(reject_modifiers(&function.sig).err());

        // The wasm signature, derived from the declared types. The two halves are
        // held to each other only once both are known.
        let params = errors::record(parse_params(&function.sig), &mut errors);
        let result = errors::record(parse_result(&function.sig), &mut errors);
        if let (Some(params), Some(result)) = (&params, result) {
            errors.extend(check_result_matches_regions(&function.sig, params, result).err());
        }

        // A name whose PascalCase form is not a legal variant is reported here
        // rather than emitted, which would either panic or fail downstream.
        let variant = errors::record(variant_ident(&function.sig.ident), &mut errors);

        if let Some(error) = errors::combine(errors) {
            return Err(error);
        }

        let (Some(gas), Some(wasm_name), Some(variant), Some(params), Some(result)) =
            (gas, wasm_name, variant, params, result)
        else {
            unreachable!("every absent field is reported above");
        };

        Ok(Self {
            gas,
            wasm_name,
            docs,
            variant,
            signature: function.sig,
            params,
            result,
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

/// The declared parameters, lowered, with the receiver skipped. Every parameter
/// is reported against its own span, so a declaration surfaces all of its
/// parameter mistakes in one build rather than one per rebuild.
fn parse_params(signature: &Signature) -> syn::Result<Vec<Param>> {
    let mut params = Vec::new();
    let mut errors = Vec::new();

    for input in &signature.inputs {
        // The receiver, which Rust's grammar puts first and nowhere else, and
        // which `check_receiver` holds to `&self`.
        let FnArg::Typed(PatType { pat, ty, .. }) = input else {
            continue;
        };
        // Both halves recorded, so a parameter that is both badly named and badly
        // typed answers for each.
        let name = errors::record(parameter_name(pat), &mut errors);
        let ty = errors::record(ParamType::parse(ty), &mut errors);
        if let (Some(name), Some(ty)) = (name, ty) {
            params.push(Param { name, ty });
        }
    }

    errors::into_result(params, errors)
}

/// The parameter's name, which must be a plain identifier: it is how the
/// declaration states what the parameter is for, the wasm signature it lowers to
/// being all `i32`s.
fn parameter_name(pat: &Pat) -> syn::Result<Ident> {
    if let Pat::Ident(PatIdent {
        by_ref: None,
        mutability: None,
        subpat: None,
        ident,
        ..
    }) = pat
    {
        return Ok(ident.clone());
    }

    Err(syn::Error::new_spanned(
        pat,
        "a host function's parameter must be a plain name, as in `seq: u32`",
    ))
}

/// `HostResult<usize>` and an output region are one fact stated twice: the length
/// answered is the length of what was written *there*, so either alone is a
/// declaration nothing can serve.
fn check_result_matches_regions(
    signature: &Signature,
    params: &[Param],
    result: ResultType,
) -> syn::Result<()> {
    let writes_a_region = params.iter().any(|param| param.ty.is_out_region());

    match (result.is_buffer_length(), writes_a_region) {
        (true, false) => Err(syn::Error::new_spanned(
            &signature.ident,
            "a host function returning `HostResult<usize>` must take an output region \
             (`&mut [u8]`): the length it answers is the length of what it wrote there",
        )),
        (false, true) => Err(syn::Error::new_spanned(
            &signature.ident,
            "a host function taking an output region (`&mut [u8]`) must return \
             `HostResult<usize>`: the length it wrote is what the guest is answered",
        )),
        (true, true) | (false, false) => Ok(()),
    }
}

/// Every declaration returns `HostResult<T>`, including the ones that yield
/// nothing (`HostResult<()>`).
///
/// One shape for every function is what lets a single dispatch adapter lower them
/// all: lift the arguments out of guest memory, call the host, then turn `Ok(T)`
/// into the wire's non-negative `i32` and `Err(e)` into a negative code or a trap.
/// A function returning a bare `T` would need its own arm.
///
/// The wrapper is checked here; which `T`s the ABI has is [`ResultType`]'s.
fn parse_result(signature: &Signature) -> syn::Result<ResultType> {
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
    // One argument, and a type: neither `HostResult<'a>` nor `HostResult<i32, i32>`
    // names a success type.
    let success = match (arguments.args.len(), arguments.args.first()) {
        (1, Some(GenericArgument::Type(success))) => success,
        _ => {
            return Err(syn::Error::new_spanned(
                arguments,
                format!("`{HOST_RESULT}` takes exactly one type: `{HOST_RESULT}<T>`"),
            ));
        }
    };

    ResultType::parse(success)
}

/// `const`, `async`, `unsafe`/`safe` and `extern "…"` have no meaning in the
/// wasm ABI, and would otherwise pass silently into the generated trait.
fn reject_modifiers(signature: &Signature) -> syn::Result<()> {
    const PLAIN: &str =
        "a host function must be a plain `fn`: this modifier is not part of the wasm ABI";

    let mut errors = Vec::new();

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

    errors::into_result((), errors)
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

/// The value of `#[name = <value>]`, parsed as `T`.
///
/// `expected` completes "`gas` expects …": syn's own message for the wrong kind
/// of literal names neither the attribute nor what it wanted.
fn value<T: Parse>(attr: &Attribute, expected: &str) -> syn::Result<T> {
    let expr = &attr.meta.require_name_value()?.value;
    syn::parse2(expr.to_token_stream()).map_err(|_| {
        syn::Error::new_spanned(expr, format!("`{}` expects {expected}", path_name(attr)))
    })
}

fn int_value(attr: &Attribute) -> syn::Result<u64> {
    let int: LitInt = value(attr, "an integer literal")?;
    // `LitInt` keeps the sign in its digits, so `base10_parse::<u64>` would
    // report a negative value as "invalid digit found in string".
    if int.base10_digits().starts_with('-') {
        return Err(syn::Error::new_spanned(
            int,
            format!("`{}` must not be negative", path_name(attr)),
        ));
    }
    int.base10_parse()
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
    use syn::{Expr, ExprLit, Lit, parse_quote};

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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert_eq!(messages[0], "`gas` must not be negative");
    }

    #[test]
    fn rejects_unusable_wasm_names() {
        let empty = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = ""]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });
        assert_eq!(empty.len(), 1, "{empty:?}");
        assert_eq!(empty[0], "the wasm name must not be empty");

        let spaced = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });
        assert_eq!(spaced.len(), 1, "{spaced:?}");
        assert!(spaced[0].contains("may only contain"), "{spaced:?}");
    }

    #[test]
    fn rejects_signature_modifiers() {
        for declaration in [
            quote! { unsafe fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>; },
            quote! { async fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>; },
            quote! { const fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>; },
            quote! { extern "C" fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>; },
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
            fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize>;
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
            method.contains(
                "fn sha512_half (& self , data : & [u8] , out : & mut [u8]) -> HostResult < usize > ;"
            ),
            "{method}"
        );
    }

    #[test]
    fn spec_arm_carries_the_name_the_gas_and_the_wasm_signature() {
        let parsed = ParsedHostFunction::parse(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        })
        .unwrap();

        assert_eq!(
            parsed.spec_arm().to_string(),
            "Self :: GetLedgerSqn => HostFnSpec { name : \"ldgr_index\" , gas : 60u64 , \
             wasm_params : & [WasmValType :: I32 , WasmValType :: I32] , \
             wasm_result : Some (WasmValType :: I32) , }"
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        })
        .unwrap();

        let docs: Vec<_> = parsed.docs.iter().map(doc_text).collect();
        assert_eq!(docs, vec![" First line.", "", " Third line."]);
    }

    #[test]
    fn preserves_parameters_and_return_type() {
        let traced = ParsedHostFunction::parse(parse_quote! {
            #[gas = 30]
            #[wasm_name = "trace"]
            fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()>;
        })
        .unwrap();

        // The receiver is `inputs[0]`; the three declared parameters follow it.
        assert_eq!(traced.signature.inputs.len(), 4);
        assert_eq!(
            traced.signature.output.to_token_stream().to_string(),
            "-> HostResult < () >"
        );
    }

    /// Why the declared parameter list and the wasm signature are different
    /// lengths: `account`, `seq` and `out` are a `(ptr, len)` pair each — `seq`
    /// too, reading like a scalar but holding four little-endian bytes.
    #[test]
    fn derives_the_wasm_signature_from_the_declared_types() {
        let keylet = ParsedHostFunction::parse(parse_quote! {
            #[gas = 350]
            #[wasm_name = "check_id"]
            fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;
        })
        .unwrap();

        assert_eq!(
            declared(&keylet),
            ["account: InBytes", "seq: InU32", "out: OutBytes"]
        );
        assert_eq!(
            keylet.wasm_params().collect::<Vec<_>>(),
            [WasmValType::I32; 6]
        );
        assert_eq!(keylet.result, ResultType::BufferLength);
    }

    /// The scalars are the exception: spelled as themselves, and in declaration
    /// order, which is wire order.
    #[test]
    fn passes_the_wasm_scalars_through_in_declaration_order() {
        let from_int = ParsedHostFunction::parse(parse_quote! {
            #[gas = 100]
            #[wasm_name = "float_from_int"]
            fn float_from_int(&self, x: i64, out: &mut [u8], mode: i32) -> HostResult<usize>;
        })
        .unwrap();

        assert_eq!(
            from_int.wasm_params().collect::<Vec<_>>(),
            [
                WasmValType::I64,
                WasmValType::I32,
                WasmValType::I32,
                WasmValType::I32
            ]
        );
    }

    /// `&self` is `inputs[0]` and is not a parameter. Read as one it would be a
    /// declared type the ABI does not have, refusing every declaration.
    #[test]
    fn does_not_read_the_receiver_as_a_parameter() {
        let array_len = ParsedHostFunction::parse(parse_quote! {
            #[gas = 40]
            #[wasm_name = "tx_arr_len"]
            fn get_tx_array_len(&self, field: i32) -> HostResult<i32>;
        })
        .unwrap();

        assert_eq!(declared(&array_len), ["field: I32"]);
        assert_eq!(array_len.result, ResultType::Value);
    }

    /// The declared parameters as `name: Type`, which is what the glue spells.
    fn declared(function: &ParsedHostFunction) -> Vec<String> {
        function
            .params()
            .iter()
            .map(|param| format!("{}: {:?}", param.name, param.ty))
            .collect()
    }

    /// One diagnostic per parameter, each on its own span, so a declaration's
    /// mistakes all surface in one build.
    #[test]
    fn rejects_parameter_types_the_abi_does_not_have() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, flag: bool, data: Vec<u8>) -> HostResult<i32>;
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
        assert!(
            messages.iter().all(|m| m.contains("must be `i32`")),
            "{messages:?}"
        );
    }

    #[test]
    fn rejects_parameters_that_are_not_plain_names() {
        for parameter in [
            quote! { _: i32 },
            quote! { mut field: i32 },
            quote! { (field, flag): i32 },
        ] {
            let function: TraitItemFn = syn::parse2(quote! {
                #[gas = 40]
                #[wasm_name = "tx_arr_len"]
                fn get_tx_array_len(&self, #parameter) -> HostResult<i32>;
            })
            .unwrap_or_else(|_| panic!("`{parameter}` should parse"));

            let messages = messages(function);
            assert!(
                messages.iter().any(|m| m.contains("must be a plain name")),
                "`{parameter}`: {messages:?}"
            );
        }
    }

    /// Two mistakes on one parameter are two diagnostics, neither standing in for
    /// the other.
    #[test]
    fn reports_a_parameter_s_name_and_its_type_separately() {
        let messages = messages(parse_quote! {
            #[gas = 40]
            #[wasm_name = "tx_arr_len"]
            fn get_tx_array_len(&self, _: bool) -> HostResult<i32>;
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
        assert!(messages[0].contains("must be a plain name"), "{messages:?}");
        assert!(messages[1].contains("must be `i32`"), "{messages:?}");
    }

    /// The success type is held to the three the ABI has. `[u8; 4]` is the one
    /// worth pinning: it says what the value *is*, which is the guest SDK's
    /// business and not the wire's.
    #[test]
    fn rejects_success_types_the_abi_does_not_have() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<[u8; 4]>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("must return `HostResult<usize>`"),
            "{messages:?}"
        );
    }

    /// Each half of the pairing refused on its own: a length with nowhere to have
    /// written the value, and a written region whose length the guest never learns.
    #[test]
    fn rejects_a_result_that_does_not_match_the_regions() {
        let no_region = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<usize>;
        });
        assert_eq!(no_region.len(), 1, "{no_region:?}");
        assert!(
            no_region[0].contains("must take an output region"),
            "{no_region:?}"
        );

        let unreported = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<i32>;
        });
        assert_eq!(unreported.len(), 1, "{unreported:?}");
        assert!(
            unreported[0].contains("must return `HostResult<usize>`"),
            "{unreported:?}"
        );
    }

    #[test]
    fn reports_both_missing_attributes_at_once() {
        let messages = messages(parse_quote! {
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });
        assert_eq!(gas.len(), 1, "{gas:?}");
        assert!(
            gas[0].contains("`gas` expects an integer literal"),
            "{gas:?}"
        );

        let name = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = 7]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("number too large"), "{messages:?}");
    }

    #[test]
    fn rejects_attribute_shapes_other_than_name_value() {
        let bare = messages(parse_quote! {
            #[gas]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });
        assert_eq!(bare.len(), 1, "{bare:?}");
        assert!(bare[0].contains("gas = ..."), "{bare:?}");

        let list = messages(parse_quote! {
            #[gas(60)]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
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
            fn get_ledger_sqn(&self) -> HostResult<i32> { Ok(0) }
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("must not have a body"), "{messages:?}");
    }

    #[test]
    fn rejects_generics() {
        let parameter = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn<T>(&self) -> HostResult<i32>;
        });
        assert_eq!(parameter.len(), 1, "{parameter:?}");
        assert!(
            parameter[0].contains("must not be generic"),
            "{parameter:?}"
        );

        let clause = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self) -> HostResult<i32> where Self: Sized;
        });
        assert_eq!(clause.len(), 1, "{clause:?}");
    }

    #[test]
    fn requires_a_receiver() {
        let messages = messages(parse_quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(out: &mut [u8]) -> HostResult<usize>;
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
                fn get_ledger_sqn(#receiver) -> HostResult<i32>;
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
            fn get_ledger_sqn(&self) -> xrpl_host_functions::HostResult<i32>;
        })
        .unwrap();

        assert!(
            parsed
                .trait_method()
                .to_string()
                .contains("xrpl_host_functions :: HostResult < i32 >"),
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
