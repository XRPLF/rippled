use proc_macro::TokenStream;

/// Function-like proc macro that echoes its input back as the body of a
/// generated `answer()` function — e.g. `answer_echo!(42)` expands to
/// `fn answer() -> u32 { 42 }`.
///
/// The value of this macro is not the expansion itself but the fact that a
/// crate using it must be compiled — which makes rustc load this crate's
/// dylib at build time. That load is what regresses to
/// "E0463 can't find crate for answer_macro" when the toolchain's libstd is
/// not resolvable for proc-macro dylibs (see nix/packages.nix).
#[proc_macro]
pub fn answer_echo(item: TokenStream) -> TokenStream {
    format!("fn answer() -> u32 {{ {item} }}").parse().unwrap()
}
