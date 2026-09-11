//! `#[coded_enum]`: an enum of wire codes, and the `ALL`/`code`/`from_code` set
//! that must not fall behind its variants.
//!
//! One list is what makes `ALL` complete. Rust cannot enumerate an enum's
//! variants — an exhaustive `match` forces an arm per variant but gives nothing to
//! iterate — so a hand-written `ALL` beside a hand-written enum could only be kept
//! in step by review, and `ALL`'s whole purpose is to be the set a test can trust.
//! A variant added to the enum gains its `ALL` entry and its `from_code` arm by
//! construction. `HostFunctionSpec::ALL` is complete the same way, from the
//! `host_functions!` block.

use proc_macro2::TokenStream;
use quote::quote;
use syn::{Expr, ExprLit, ExprUnary, Fields, Ident, ItemEnum, Lit, UnOp, Variant};

use crate::errors;

/// One variant as the expansion reads it: the name `ALL` lists and the code
/// `from_code` matches.
struct WireVariant<'a> {
    ident: &'a Ident,
    code: &'a Expr,
}

pub(crate) fn expand(args: TokenStream, item: TokenStream) -> syn::Result<TokenStream> {
    if !args.is_empty() {
        return Err(syn::Error::new_spanned(
            args,
            "`#[coded_enum]` takes no arguments",
        ));
    }

    let item: ItemEnum = syn::parse2(item)?;
    let variants = wire_variants(&item)?;

    let attrs = &item.attrs;
    let vis = &item.vis;
    let name = &item.ident;
    let declarations = item.variants.iter();
    let idents = variants.iter().map(|variant| variant.ident);
    let arms = variants.iter().map(|variant| {
        let (ident, code) = (variant.ident, variant.code);
        quote! { #code => Some(Self::#ident) }
    });

    Ok(quote! {
        #(#attrs)*
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        #[repr(i32)]
        #vis enum #name {
            #(#declarations,)*
        }

        impl #name {
            /// Every variant, in declaration order — the whole set, and whole by
            /// construction.
            pub const ALL: &'static [Self] = &[#(Self::#idents,)*];

            /// The wire value that names this variant.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// The variant `code` names, or `None` if no variant does.
            ///
            /// What an unnamed code means is left to the caller, being a different
            /// condition per enum.
            pub const fn from_code(code: i32) -> Option<Self> {
                match code {
                    #(#arms,)*
                    _ => None,
                }
            }
        }
    })
}

/// Every variant, checked against what the expansion needs of it, or every
/// mistake in the list.
fn wire_variants(item: &ItemEnum) -> syn::Result<Vec<WireVariant<'_>>> {
    // `#[repr(i32)]` is rejected on a variantless enum, and there is nothing for a
    // wire enum with no codes to mean anyway.
    if item.variants.is_empty() {
        return Err(syn::Error::new_spanned(
            item,
            "`#[coded_enum]` needs at least one variant",
        ));
    }

    let mut errors = Vec::new();
    let variants = item
        .variants
        .iter()
        .filter_map(|variant| {
            let code = errors::record(code_of(variant), &mut errors)?;
            Some(WireVariant {
                ident: &variant.ident,
                code,
            })
        })
        .collect();

    errors::into_result(variants, errors)
}

/// The code a variant is declared with.
fn code_of(variant: &Variant) -> syn::Result<&Expr> {
    if !matches!(variant.fields, Fields::Unit) {
        return Err(syn::Error::new_spanned(
            &variant.fields,
            "a wire enum's variants carry no data: the code is the whole of what crosses",
        ));
    }

    let Some((_, code)) = &variant.discriminant else {
        return Err(syn::Error::new_spanned(
            variant,
            "missing `= <code>`: a wire value is declared, never implied by position",
        ));
    };

    if !is_integer_literal(code) {
        return Err(syn::Error::new_spanned(
            code,
            "a wire value must be an integer literal, since `from_code` matches it as a pattern",
        ));
    }

    Ok(code)
}

/// `-2147483648` and `7`, but not `i32::MIN` or `1 + 1`: the discriminant is
/// emitted into pattern position unchanged, where an expression is either a
/// different meaning or no meaning at all.
fn is_integer_literal(code: &Expr) -> bool {
    match code {
        Expr::Lit(ExprLit {
            lit: Lit::Int(_), ..
        }) => true,
        Expr::Unary(ExprUnary {
            op: UnOp::Neg(_),
            expr,
            ..
        }) => is_integer_literal(expr),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The whole expansion for the smallest list that exercises every generated
    /// item, negative codes included.
    #[test]
    fn generates_the_enum_and_the_three_items_over_it() {
        let generated = generated(quote! {
            /// How a trace buffer is read.
            pub enum TraceDataType {
                /// Eight little-endian bytes.
                Int64 = 1,
                Unnamed = -2,
            }
        });

        for expected in [
            // The declaration reaches the output as written, doc comments and all,
            // under the derives and the representation `code` casts through.
            "# [doc = r\" How a trace buffer is read.\"] \
             # [derive (Debug , Clone , Copy , PartialEq , Eq)] # [repr (i32)] \
             pub enum TraceDataType { # [doc = r\" Eight little-endian bytes.\"] Int64 = 1 , \
             Unnamed = - 2 , }",
            "pub const ALL : & 'static [Self] = & [Self :: Int64 , Self :: Unnamed ,] ;",
            "pub const fn code (self) -> i32 { self as i32 }",
            "pub const fn from_code (code : i32) -> Option < Self > \
             { match code { 1 => Some (Self :: Int64) , - 2 => Some (Self :: Unnamed) , \
             _ => None , } }",
        ] {
            assert!(generated.contains(expected), "missing {expected:?}");
        }
    }

    /// The visibility is the caller's: these enums are the ABI crate's public
    /// vocabulary, and a `pub` the macro supplied would be one the declaration
    /// could not take back.
    #[test]
    fn keeps_the_declared_visibility() {
        assert!(
            generated(quote! {
                enum Private {
                    One = 1,
                }
            })
            .contains("enum Private"),
            "the expansion should not widen a private enum"
        );
    }

    /// A variant with no code would take one from its position, which is the
    /// mistake that silently renumbers a wire value.
    #[test]
    fn rejects_a_variant_without_a_code() {
        let messages = messages(quote! {
            pub enum Ordering {
                Equal = 0,
                Greater,
            }
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("missing `= <code>`"), "{messages:?}");
    }

    /// Both halves of what a code must be: a plain integer, and the only thing the
    /// variant carries.
    #[test]
    fn rejects_a_code_that_is_not_an_integer_literal() {
        let messages = messages(quote! {
            pub enum Ordering {
                Equal = i32::MIN,
                Greater = 1 + 1,
            }
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
        for message in &messages {
            assert!(message.contains("must be an integer literal"), "{message}");
        }
    }

    #[test]
    fn rejects_a_variant_carrying_data() {
        let messages = messages(quote! {
            pub enum Ordering {
                Equal(u8) = 0,
            }
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("carry no data"), "{messages:?}");
    }

    /// `#[repr(i32)]` is rejected on a variantless enum, so the diagnostic has to
    /// be this one rather than rustc's.
    #[test]
    fn rejects_an_enum_with_no_variants() {
        let messages = messages(quote! {
            pub enum Nothing {}
        });

        assert_eq!(messages.len(), 1, "{messages:?}");
        assert!(messages[0].contains("at least one variant"), "{messages:?}");
    }

    /// Every mistake in one build, as `host_functions!` reports a block.
    #[test]
    fn reports_every_mistake_in_the_list() {
        let messages = messages(quote! {
            pub enum Ordering {
                Equal,
                Greater = 1 + 1,
            }
        });

        assert_eq!(messages.len(), 2, "{messages:?}");
    }

    #[test]
    fn rejects_arguments() {
        let error = expand(quote!(i64), quote! { pub enum Ordering { Equal = 0, } })
            .expect_err("expected the argument to be refused");

        assert!(error.to_string().contains("takes no arguments"));
    }

    #[test]
    fn rejects_an_item_that_is_not_an_enum() {
        expand(quote!(), quote! { pub struct Ordering; })
            .expect_err("expected a struct to be refused");
    }

    fn generated(item: TokenStream) -> String {
        expand(quote!(), item)
            .expect("the enum should expand")
            .to_string()
    }

    /// The messages of every diagnostic recorded by one failed `expand`.
    fn messages(item: TokenStream) -> Vec<String> {
        let Err(error) = expand(quote!(), item) else {
            panic!("expected expansion to fail");
        };
        error.into_iter().map(|error| error.to_string()).collect()
    }
}
