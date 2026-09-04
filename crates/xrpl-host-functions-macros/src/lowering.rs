//! The wire shape of a declaration: everything a declared Rust type decides — the
//! wasm value types it lowers to, the names those wasm parameters take, the type a
//! generated body is handed for it, and which of the ABI's two argument traits
//! builds that type. Kept as one set of `match` arms because the four must agree.
//!
//! The whole mapping, and the only place it is written down: a type no arm here
//! names is a type the ABI does not have, not one that falls back to something.
//!
//! Two rows are worth knowing before reading a declaration:
//!
//! - **`u32` is not a scalar.** It is a `(ptr, len)` region holding four
//!   little-endian bytes, which is how the guest SDK passes a sequence number.
//! - **`usize` and `i32` results are the same on the wire and not
//!   interchangeable**: the first is the length of what was written to an output
//!   region, the second the answer itself.
//!
//! Matching is on types as they are spelled — a proc macro resolves nothing, so
//! `type Bytes = u32; … x: Bytes` is unrecognisable — but on a path's last
//! segment, so any of these types may be spelled qualified.

use proc_macro2::TokenStream;
use quote::{ToTokens, format_ident, quote};
use syn::{Ident, PathArguments, Type, TypePath, TypeReference};

/// What a host function may be handed, and what each costs on the wire.
///
/// Declaration order is wasm parameter order, so a reader of a declaration is
/// reading the import the guest links against.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum ParamType {
    /// `i32`, passed through as itself. Also the spelling for a raw scalar
    /// whose signedness the ABI does not fix.
    I32,
    /// `i64`, passed through as itself.
    I64,
    /// `TraceDataType`: an `i32` code the engine resolves to the enum before a
    /// host sees it.
    TraceDataType,
    /// `&[u8]`: a borrowed input region.
    InBytes,
    /// `&str`: an input region whose read is also the UTF-8 check.
    InStr,
    /// `u32`: an input region holding four little-endian bytes.
    InU32,
    /// `&mut [u8]`: the writable output region.
    OutBytes,
}

/// The success type of the `HostResult<T>` every declaration returns. These three
/// are what the ABI has; any other `T` is an error.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum ResultType {
    /// `usize`: the true length of a value written to an output region, which
    /// the engine turns into the wire's `i32` or into `BufferTooSmall` /
    /// `DataFieldTooLarge`. Never itself the wire type.
    BufferLength,
    /// `i32`: the answer, from a function that writes no region.
    Value,
    /// `()`: no wasm result at all — the call's whole effect is on the host, and
    /// an `Err` reaches the guest in no form.
    Nothing,
}

/// The wasm value types this ABI uses, mirroring `xrpl_host_functions::WasmValType`.
///
/// Mirrored rather than shared because the dependency runs the other way: the ABI
/// crate depends on this one, so nothing here can name its types. The [`ToTokens`]
/// impl below is the whole of the crossing, and emits references to that enum's
/// variants — so falling out of sync with it is a compile error at the
/// `host_functions!` call site rather than drift.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum WasmValType {
    I32,
    I64,
}

impl ParamType {
    /// Recognises the declared type, or refuses it against its own span.
    pub(crate) fn parse(ty: &Type) -> syn::Result<Self> {
        const ALLOWED: &str = "a host function's parameter must be `i32`, `i64`, `u32`, \
                               `&[u8]`, `&mut [u8]`, `&str` or `TraceDataType`";

        let recognised = match ty {
            // A lifetime on the reference changes nothing on the wire.
            Type::Reference(TypeReference {
                mutability, elem, ..
            }) => match (mutability, &**elem) {
                (None, Type::Slice(slice)) if is_named(&slice.elem, "u8") => Some(Self::InBytes),
                (Some(_), Type::Slice(slice)) if is_named(&slice.elem, "u8") => {
                    Some(Self::OutBytes)
                }
                (None, elem) if is_named(elem, "str") => Some(Self::InStr),
                _ => None,
            },
            _ => match last_path_segment(ty) {
                Some(name) if name == "i32" => Some(Self::I32),
                Some(name) if name == "i64" => Some(Self::I64),
                Some(name) if name == "u32" => Some(Self::InU32),
                Some(name) if name == "TraceDataType" => Some(Self::TraceDataType),
                _ => None,
            },
        };

        recognised.ok_or_else(|| syn::Error::new_spanned(ty, ALLOWED))
    }

    /// The wasm parameters this declared type lowers to, in order. `InBytes` and
    /// `OutBytes` lower alike, so a region's direction survives only in the
    /// variant.
    pub(crate) fn as_wasm_params(self) -> &'static [WasmValType] {
        match self {
            Self::I32 | Self::TraceDataType => &[WasmValType::I32],
            Self::I64 => &[WasmValType::I64],
            Self::InBytes | Self::InStr | Self::InU32 | Self::OutBytes => {
                &[WasmValType::I32, WasmValType::I32]
            }
        }
    }

    /// What a declaration calls each of those wasm parameters: the declared name
    /// for a scalar, and `{name}_ptr`/`{name}_len` for the pair a region lowers
    /// to.
    ///
    /// **It must answer as many names as [`Self::as_wasm_params`] answers types**,
    /// since the generated closure declares them one against the other — hence the
    /// matching arms, and `lowers_every_declared_parameter_type`'s row-by-row
    /// length check.
    pub(crate) fn wasm_names(self, name: &Ident) -> Vec<Ident> {
        match self {
            Self::I32 | Self::I64 | Self::TraceDataType => vec![name.clone()],
            Self::InBytes | Self::InStr | Self::InU32 | Self::OutBytes => {
                vec![format_ident!("{name}_ptr"), format_ident!("{name}_len")]
            }
        }
    }

    /// The type a generated body takes this parameter as: a wasm scalar spelled as
    /// itself, everything else the argument type carrying its shape and direction —
    /// which is what makes an input region used as an output one a compile error
    /// naming both.
    ///
    /// The argument types are the engine's, so `vm` is the path they are reached
    /// under; which types need it is decided here, a wasm scalar being `i32` under
    /// every engine.
    pub(crate) fn argument_type(self, vm: &TokenStream) -> TokenStream {
        match self {
            Self::I32 => quote!(i32),
            Self::I64 => quote!(i64),
            Self::TraceDataType => quote!(#vm::TraceCode),
            Self::InBytes => quote!(#vm::InBytes),
            Self::InStr => quote!(#vm::InStr),
            Self::InU32 => quote!(#vm::InU32),
            Self::OutBytes => quote!(#vm::OutBytes),
        }
    }

    /// Which of the ABI's two argument traits builds this parameter's argument
    /// type, or `None` for a wasm scalar, which reaches a body as itself.
    ///
    /// The arity is the whole of the distinction — `FromWasmRegion` takes the two
    /// of a `(ptr, len)` pair, `FromWasmScalar` the one of a code — so this answers
    /// alongside [`Self::as_wasm_params`] rather than from a predicate elsewhere.
    pub(crate) fn argument_trait(self) -> Option<TokenStream> {
        match self {
            Self::I32 | Self::I64 => None,
            Self::TraceDataType => Some(quote!(FromWasmScalar)),
            Self::InBytes | Self::InStr | Self::InU32 | Self::OutBytes => {
                Some(quote!(FromWasmRegion))
            }
        }
    }

    /// Whether this parameter is a region the host writes to — what
    /// [`ResultType::BufferLength`] is the length *of*.
    pub(crate) fn is_out_region(self) -> bool {
        matches!(self, Self::OutBytes)
    }
}

impl ResultType {
    /// Recognises the success type of a declaration's `HostResult<T>`, or
    /// refuses it against its own span.
    pub(crate) fn parse(success: &Type) -> syn::Result<Self> {
        const ALLOWED: &str = "a host function must return `HostResult<usize>` for a value it \
                               writes to an output region, `HostResult<i32>` for one it answers \
                               directly, or `HostResult<()>` for none at all";

        if let Type::Tuple(tuple) = success
            && tuple.elems.is_empty()
        {
            return Ok(Self::Nothing);
        }

        match last_path_segment(success) {
            Some(name) if name == "usize" => Ok(Self::BufferLength),
            Some(name) if name == "i32" => Ok(Self::Value),
            _ => Err(syn::Error::new_spanned(success, ALLOWED)),
        }
    }

    /// The generated table's `wasm_result` field: `Some(WasmValType::I32)`, or
    /// `None` for the function that answers nothing.
    ///
    /// Spelled out here rather than left to `quote`'s `Option` impl, which emits
    /// nothing at all for `None`.
    pub(crate) fn wasm_result_tokens(self) -> TokenStream {
        match self.as_wasm_result() {
            Some(val_type) => quote! { Some(#val_type) },
            None => quote! { None },
        }
    }

    /// Whether the value reaches the guest as the length of what was written to
    /// an output region.
    pub(crate) fn is_buffer_length(self) -> bool {
        matches!(self, Self::BufferLength)
    }

    /// The wasm result, which does not distinguish a length from a value.
    fn as_wasm_result(self) -> Option<WasmValType> {
        match self {
            Self::BufferLength | Self::Value => Some(WasmValType::I32),
            Self::Nothing => None,
        }
    }
}

/// `WasmValType::I32` — the ABI crate's variant, named but never defined here.
impl ToTokens for WasmValType {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        tokens.extend(match self {
            Self::I32 => quote! { WasmValType::I32 },
            Self::I64 => quote! { WasmValType::I64 },
        });
    }
}

/// The last segment of a plain path type, when it carries no generic arguments:
/// `i32`, `core::primitive::i32` and `TraceDataType` all answer their own name,
/// `Vec<u8>` and `[u8; 4]` nothing.
fn last_path_segment(ty: &Type) -> Option<&Ident> {
    let Type::Path(TypePath {
        qself: None, path, ..
    }) = ty
    else {
        return None;
    };
    let last = path.segments.last()?;
    matches!(last.arguments, PathArguments::None).then_some(&last.ident)
}

/// Whether `ty` is the named primitive, however it is spelled.
fn is_named(ty: &Type, name: &str) -> bool {
    last_path_segment(ty).is_some_and(|segment| segment == name)
}

#[cfg(test)]
mod tests {
    use super::*;
    use syn::parse_quote;

    use WasmValType::{I32, I64};

    /// Every declared parameter type and everything it decides: what it costs on
    /// the wire, what a body is handed for it, and which trait builds that.
    ///
    /// The names are asserted by length rather than spelling, since a type
    /// answering fewer names than value types is the one way these answers can
    /// contradict each other.
    #[test]
    fn lowers_every_declared_parameter_type() {
        let mapping: [(Type, &[WasmValType], &str, Option<&str>); 7] = [
            (parse_quote!(i32), &[I32], "i32", None),
            (parse_quote!(i64), &[I64], "i64", None),
            (
                parse_quote!(TraceDataType),
                &[I32],
                "vm :: TraceCode",
                Some("FromWasmScalar"),
            ),
            (
                parse_quote!(&[u8]),
                &[I32, I32],
                "vm :: InBytes",
                Some("FromWasmRegion"),
            ),
            (
                parse_quote!(&str),
                &[I32, I32],
                "vm :: InStr",
                Some("FromWasmRegion"),
            ),
            (
                parse_quote!(u32),
                &[I32, I32],
                "vm :: InU32",
                Some("FromWasmRegion"),
            ),
            (
                parse_quote!(&mut [u8]),
                &[I32, I32],
                "vm :: OutBytes",
                Some("FromWasmRegion"),
            ),
        ];
        let declared_name = format_ident!("seq");
        let vm = quote!(vm);

        for (declared, wasm, argument, argument_trait) in mapping {
            let param = ParamType::parse(&declared)
                .unwrap_or_else(|_| panic!("`{}` should be a parameter type", quoted(&declared)));

            assert_eq!(param.as_wasm_params(), wasm, "`{}`", quoted(&declared));
            assert_eq!(
                param.argument_type(&vm).to_string(),
                argument,
                "`{}`",
                quoted(&declared)
            );
            assert_eq!(
                param
                    .argument_trait()
                    .map(|name| name.to_string())
                    .as_deref(),
                argument_trait,
                "`{}`",
                quoted(&declared)
            );
            assert_eq!(
                param.wasm_names(&declared_name).len(),
                wasm.len(),
                "one name per wasm parameter: `{}`",
                quoted(&declared)
            );
        }
    }

    /// A region's two wasm parameters are named off the declaration, so the
    /// generated closure reads as the declaration does.
    #[test]
    fn names_a_region_s_pair_after_the_declared_parameter() {
        let seq = format_ident!("seq");

        let names = |declared: Type| {
            ParamType::parse(&declared)
                .expect("a parameter type")
                .wasm_names(&seq)
                .iter()
                .map(Ident::to_string)
                .collect::<Vec<_>>()
        };

        assert_eq!(names(parse_quote!(u32)), ["seq_ptr", "seq_len"]);
        assert_eq!(names(parse_quote!(i32)), ["seq"]);
    }

    /// The two `(ptr, len)` pairs lower alike but are told apart, since only the
    /// direction says who may write to the region.
    #[test]
    fn keeps_the_regions_apart() {
        let input: Type = parse_quote!(&[u8]);
        let output: Type = parse_quote!(&mut [u8]);

        assert!(!ParamType::parse(&input).unwrap().is_out_region());
        assert!(ParamType::parse(&output).unwrap().is_out_region());
    }

    /// A type outside the mapping is refused rather than lowered to a guess.
    #[test]
    fn refuses_parameter_types_outside_the_mapping() {
        let outside: [Type; 11] = [
            parse_quote!(u64),
            parse_quote!(u8),
            parse_quote!(usize),
            parse_quote!(bool),
            parse_quote!(Vec<u8>),
            parse_quote!([u8; 4]),
            parse_quote!(&mut str),
            parse_quote!(&i32),
            parse_quote!(&[i32]),
            parse_quote!(&Foo),
            parse_quote!(()),
        ];

        for declared in outside {
            let Err(error) = ParamType::parse(&declared) else {
                panic!("`{}` should not be a parameter type", quoted(&declared));
            };
            assert!(
                error.to_string().contains("must be `i32`"),
                "`{}`: {error}",
                quoted(&declared)
            );
        }
    }

    /// The three success types, and the wasm result each becomes. `usize` and
    /// `i32` agree on the wire and are separate rows.
    #[test]
    fn lowers_every_success_type() {
        let mapping: [(Type, ResultType, Option<WasmValType>); 3] = [
            (parse_quote!(usize), ResultType::BufferLength, Some(I32)),
            (parse_quote!(i32), ResultType::Value, Some(I32)),
            (parse_quote!(()), ResultType::Nothing, None),
        ];

        for (declared, expected, wasm_result) in mapping {
            let result = ResultType::parse(&declared)
                .unwrap_or_else(|_| panic!("`{}` should be a success type", quoted(&declared)));

            assert_eq!(result, expected, "`{}`", quoted(&declared));
            assert_eq!(
                result.as_wasm_result(),
                wasm_result,
                "`{}`",
                quoted(&declared)
            );
        }
    }

    /// The distinction the wasm result loses: which of the two `i32` results was
    /// declared decides how the value reaches the guest.
    #[test]
    fn tells_a_length_from_a_value() {
        assert!(ResultType::BufferLength.is_buffer_length());
        assert!(!ResultType::Value.is_buffer_length());
        assert!(!ResultType::Nothing.is_buffer_length());
    }

    #[test]
    fn refuses_success_types_outside_the_mapping() {
        let outside: [Type; 6] = [
            parse_quote!(u32),
            parse_quote!(i64),
            parse_quote!(bool),
            parse_quote!([u8; 32]),
            parse_quote!(Vec<u8>),
            parse_quote!((usize, i32)),
        ];

        for declared in outside {
            let Err(error) = ResultType::parse(&declared) else {
                panic!("`{}` should not be a success type", quoted(&declared));
            };
            assert!(
                error
                    .to_string()
                    .contains("must return `HostResult<usize>`"),
                "`{}`: {error}",
                quoted(&declared)
            );
        }
    }

    /// A qualified spelling is the same type, matching how the return type finds
    /// `HostResult`.
    #[test]
    fn accepts_qualified_spellings() {
        let qualified: Type = parse_quote!(core::primitive::i32);
        assert_eq!(ParamType::parse(&qualified).unwrap(), ParamType::I32);

        let qualified: Type = parse_quote!(xrpl_host_functions::TraceDataType);
        assert_eq!(
            ParamType::parse(&qualified).unwrap(),
            ParamType::TraceDataType
        );
    }

    /// The emitted tokens name the ABI crate's variants, which is the whole of
    /// what crosses out of this crate. Pinned here so a break in the mirror is a
    /// failure with a span rather than a rustc error at the call site.
    #[test]
    fn emits_references_to_the_hand_written_variants() {
        assert_eq!(I32.to_token_stream().to_string(), "WasmValType :: I32");
        assert_eq!(I64.to_token_stream().to_string(), "WasmValType :: I64");

        assert_eq!(
            ResultType::BufferLength.wasm_result_tokens().to_string(),
            "Some (WasmValType :: I32)"
        );
        assert_eq!(ResultType::Nothing.wasm_result_tokens().to_string(), "None");
    }

    fn quoted(ty: &Type) -> String {
        ty.to_token_stream().to_string()
    }
}
