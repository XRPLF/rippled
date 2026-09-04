mod errors;
mod glue;
mod lowering;
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
/// hand-writes the types the declarations refer to and holds the one declaration
/// block.
///
/// # What it generates
///
/// Four items, in the scope the block is written in:
///
/// - `pub trait HostFunctions`: one method per declaration, emitted verbatim —
///   receiver, parameters, return type and doc comment exactly as written. An
///   execution environment implements it; the rest of the expansion does not
///   mention it.
/// - `pub enum HostFunctionSpec`: one variant per declaration, named by
///   PascalCasing the function name (`get_ledger_sqn` becomes `GetLedgerSqn`) and
///   carrying that declaration's doc comment. Its `const fn wasm_name`, `gas`,
///   `wasm_params` and `wasm_result` are the ABI metadata, and `ALL` is every
///   variant in declaration order — what a wasm engine iterates to build its
///   import table.
/// - `struct HostFnSpec`: private, one row of that metadata table. It exists only
///   so those accessors read from a single `match` over the declarations, and
///   never appears in a signature a caller can name.
/// - `macro_rules! wasmi_glue`: the registration for a wasmi engine, emitted as a
///   macro rather than as code because it names an engine this crate must not
///   depend on. Inert until expanded — see its own documentation.
///
/// The wasm signature is derived from the declared types rather than stated a
/// second time: `i32` and `i64` are the wasm scalars spelled as themselves, every
/// other parameter type is marshalled through a `(ptr, len)` pair or an `i32`
/// code, and the result comes from the `HostResult<T>` success type. The glue is
/// generated from that same derivation, so the closure a guest links against and
/// the signature it is screened by are one statement.
///
/// Outside the glue's body the expansion builds only `Self::Variant` and
/// `WasmValType` paths, so the block compiles wherever the types it names —
/// `HostResult` and `WasmValType` — resolve.
///
/// ```
/// use xrpl_host_functions::{HostResult, WasmValType};
/// use xrpl_host_functions_macros::host_functions;
///
/// host_functions! {
///     /// The sequence number of the ledger being built, as 4 little-endian bytes.
///     #[gas = 60]
///     #[wasm_name = "ldgr_index"]
///     fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
///
///     /// Writes `msg` to the trace log.
///     #[gas = 500]
///     #[wasm_name = "trace_num"]
///     fn trace_num(&self, msg: &str, number: i64) -> HostResult<()>;
/// }
///
/// // The trait's methods are the declarations, down to the `&self` receiver the
/// // VM calls the host through.
/// fn ledger_sqn(host: &dyn HostFunctions, out: &mut [u8]) -> HostResult<usize> {
///     host.get_ledger_sqn(out)
/// }
///
/// // The metadata is a `const` table, so gas and import names are available at
/// // compile time rather than looked up at run time.
/// const TRACE_GAS: u64 = HostFunctionSpec::TraceNum.gas();
/// assert_eq!(TRACE_GAS, 500);
///
/// assert_eq!(HostFunctionSpec::GetLedgerSqn.wasm_name(), "ldgr_index");
/// assert_eq!(
///     HostFunctionSpec::ALL,
///     &[HostFunctionSpec::GetLedgerSqn, HostFunctionSpec::TraceNum],
/// );
///
/// // So is the wasm signature: `out: &mut [u8]` is the pair `(ptr, len)`, and
/// // `HostResult<usize>` answers the length written to it.
/// assert_eq!(
///     HostFunctionSpec::GetLedgerSqn.wasm_params(),
///     &[WasmValType::I32, WasmValType::I32],
/// );
/// assert_eq!(
///     HostFunctionSpec::GetLedgerSqn.wasm_result(),
///     Some(WasmValType::I32),
/// );
///
/// // `trace_num` answers nothing at all, so its import has no result.
/// assert_eq!(
///     HostFunctionSpec::TraceNum.wasm_params(),
///     &[WasmValType::I32, WasmValType::I32, WasmValType::I64],
/// );
/// assert_eq!(HostFunctionSpec::TraceNum.wasm_result(), None);
/// ```
///
/// A declaration must be a plain `fn` taking `&self`, with no body and no
/// generics: it maps to exactly one wasm import signature. Its parameters must be
/// `i32`, `i64`, `u32`, `&[u8]`, `&mut [u8]`, `&str` or `TraceDataType`, and it
/// must return `HostResult<usize>` if it writes an output region,
/// `HostResult<i32>` if it answers a value directly, or `HostResult<()>` if it
/// answers nothing. Two declarations may not share a `wasm_name`, nor collapse to
/// the same PascalCase variant.
#[proc_macro]
pub fn host_functions(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    expand(input.into())
        .unwrap_or_else(syn::Error::into_compile_error)
        .into()
}

fn expand(input: TokenStream) -> syn::Result<TokenStream> {
    let functions = parse_block(input)?;
    let abi = abi_items(&functions);
    let glue = glue::wasmi_glue(&functions);

    Ok(quote! {
        #abi
        #glue
    })
}

/// Every declaration in the block, parsed and checked against each other, or
/// every mistake in it.
fn parse_block(input: TokenStream) -> syn::Result<Vec<ParsedHostFunction>> {
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

    Ok(parsed)
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

/// The ABI itself: the trait a host implements and the table everything else
/// reads. `glue::wasmi_glue` is the other half of the expansion.
fn abi_items(functions: &[ParsedHostFunction]) -> TokenStream {
    let trait_methods = functions.iter().map(ParsedHostFunction::trait_method);
    let variants = functions
        .iter()
        .map(ParsedHostFunction::variant_declaration);
    let spec_arms = functions.iter().map(ParsedHostFunction::spec_arm);
    let all = functions.iter().map(|function| &function.variant);

    quote! {
        /// The host side of the wasm ABI: one method per function a guest may
        /// import.
        ///
        /// Implement it once per execution environment — the ledger host, a test
        /// double, a benchmark fake — and a guest module cannot tell them apart.
        /// Each method is one declaration from the `host_functions!` block, as
        /// written; its `&self` receiver is not part of the ABI the guest sees,
        /// so a host that must mutate does so behind interior mutability.
        ///
        /// # The output contract
        ///
        /// A method handed an `out` buffer **writes into it only when the whole
        /// value fits, and returns the value's true length whether it fitted or
        /// not.**
        ///
        /// The length is the value's, not the number of bytes written, because it
        /// is how a guest that asked with too small a buffer learns the size to
        /// ask for next time. The engine turns a length past the buffer into
        /// `BufferTooSmall`, and one past the field cap into `DataFieldTooLarge`,
        /// so a host needs to know neither.
        ///
        /// Writing nothing unless the value fits is the half only a host can hold
        /// up. An engine can bound how many bytes are *writable* — and does, by
        /// handing over a region clamped to the field cap — but it cannot take
        /// back what a method already put there. A host that wrote a truncated
        /// prefix and then reported the larger length would leave those bytes in
        /// guest memory behind a refusal the guest is told to ignore.
        pub trait HostFunctions {
            #(#trait_methods)*
        }

        /// One row of the ABI table: what [`HostFunctionSpec`]'s accessors
        /// read from.
        ///
        /// Private, and the only reason it exists is to keep all of them fed
        /// from a single `match` over the declarations.
        struct HostFnSpec {
            name: &'static str,
            gas: u64,
            wasm_params: &'static [WasmValType],
            wasm_result: Option<WasmValType>,
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

            /// This function's row of the ABI table.
            const fn spec(self) -> HostFnSpec {
                match self {
                    #(#spec_arms,)*
                }
            }

            /// The name a guest imports this function under.
            ///
            /// A guest's import name must match this exactly, or the module
            /// fails to instantiate. Usable in `const` context, so import lists
            /// can be built at compile time.
            pub const fn wasm_name(self) -> &'static str {
                self.spec().name
            }

            /// Gas charged before the call runs, independent of its arguments.
            ///
            /// Consensus-relevant: two nodes that disagree on this value
            /// disagree on transaction outcomes. Usable in `const` context, so
            /// gas tables can be built at compile time.
            pub const fn gas(self) -> u64 {
                self.spec().gas
            }

            /// The wasm parameters this function is imported with, in wire
            /// order — the list a guest's import must match, which the
            /// declaration's own parameter list is not: a declared parameter
            /// marshalled through a `(ptr, len)` region is two of these.
            ///
            /// Usable in `const` context, so import lists can be built at
            /// compile time.
            pub const fn wasm_params(self) -> &'static [WasmValType] {
                self.spec().wasm_params
            }

            /// The wasm result this function answers with, or `None` for the
            /// one whose whole effect is on the host. An `i32` where there is
            /// one, whether the host answered a value or the length of what it
            /// wrote — the wire does not distinguish those.
            pub const fn wasm_result(self) -> Option<WasmValType> {
                self.spec().wasm_result
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
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;

            #[gas = 2000]
            fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize>;
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

    /// The ABI half of the expansion alone: the glue's body is written against
    /// another crate entirely, so the tests below about what the expansion may
    /// name are not about it.
    fn abi_expansion(input: TokenStream) -> String {
        abi_items(&parse_block(input).expect("the block should parse")).to_string()
    }

    #[test]
    fn generates_the_trait_the_enum_and_the_table() {
        let generated = expand(quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;

            #[gas = 500]
            #[wasm_name = "trace_num"]
            fn trace_num(&self, msg: &str, number: i64) -> HostResult<()>;
        })
        .unwrap()
        .to_string();

        for expected in [
            "pub trait HostFunctions",
            "fn get_ledger_sqn (& self , out : & mut [u8]) -> HostResult < usize > ;",
            "fn trace_num (& self , msg : & str , number : i64) -> HostResult < () > ;",
            "pub enum HostFunctionSpec { GetLedgerSqn , TraceNum , }",
            "pub const ALL : & 'static [Self] = & [Self :: GetLedgerSqn , Self :: TraceNum ,]",
            // The table's row type is generated too, and stays private.
            "struct HostFnSpec { name : & 'static str , gas : u64 , \
             wasm_params : & 'static [WasmValType] , wasm_result : Option < WasmValType > , }",
            "const fn spec (self) -> HostFnSpec",
            // Two wasm parameters for the one declared region, and a result for
            // the length written to it.
            "Self :: GetLedgerSqn => HostFnSpec { name : \"ldgr_index\" , gas : 60u64 , \
             wasm_params : & [WasmValType :: I32 , WasmValType :: I32] , \
             wasm_result : Some (WasmValType :: I32) , }",
            "Self :: TraceNum => HostFnSpec { name : \"trace_num\" , gas : 500u64 , \
             wasm_params : & [WasmValType :: I32 , WasmValType :: I32 , WasmValType :: I64] , \
             wasm_result : None , }",
            "pub const fn wasm_name (self) -> & 'static str",
            "pub const fn gas (self) -> u64",
            "pub const fn wasm_params (self) -> & 'static [WasmValType]",
            "pub const fn wasm_result (self) -> Option < WasmValType >",
            // The fourth item; its contents are `glue`'s own tests.
            "macro_rules ! wasmi_glue",
        ] {
            assert!(generated.contains(expected), "missing {expected:?}");
        }
    }

    /// The ABI reaches for nothing outside the crate it lands in, which is what
    /// lets that crate stay zero-dependency and link into the guest. The glue is
    /// not held to this — its body names one engine throughout, and `glue`'s own
    /// tests pin that instead.
    #[test]
    fn names_no_crate_of_its_own() {
        let generated = abi_expansion(quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });

        assert!(!generated.contains("xrpl_host_functions"), "{generated}");

        // Two roots and no others: `Self::Variant` and `WasmValType::I32`.
        // Doc comments spell paths without spaces (`Self::ALL`), so they do not
        // match.
        for (index, _) in generated.match_indices(" :: ") {
            let prefix = &generated[..index];
            assert!(
                prefix.ends_with("Self") || prefix.ends_with("WasmValType"),
                "path out of the expansion at {index}: {generated}"
            );
        }
    }

    /// `spec` is an implementation detail of the two accessors, so it must not
    /// become part of the ABI crate's public surface.
    #[test]
    fn keeps_the_table_row_private() {
        let generated = abi_expansion(quote! {
            #[gas = 60]
            #[wasm_name = "ldgr_index"]
            fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;
        });

        assert!(!generated.contains("pub struct HostFnSpec"), "{generated}");
        assert!(!generated.contains("pub const fn spec"), "{generated}");
    }

    #[test]
    fn rejects_two_functions_that_share_a_wasm_name() {
        let messages = messages(quote! {
            #[gas = 60]
            #[wasm_name = "trace"]
            fn trace(&self, msg: &str) -> HostResult<()>;

            #[gas = 70]
            #[wasm_name = "trace"]
            fn trace_num(&self, msg: &str, number: i64) -> HostResult<()>;
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
            fn get_ledger_sqn(&self) -> HostResult<i32>;

            #[gas = 70]
            #[wasm_name = "b"]
            fn get_ledger__sqn(&self) -> HostResult<i32>;
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(
            messages[0].contains("`GetLedgerSqn` variant"),
            "{messages:?}"
        );
    }
}
