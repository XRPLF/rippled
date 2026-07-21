use proc_macro::TokenStream;

/// Function-like proc macro that echoes its input back as the body of a
/// generated `echo()` function — e.g. `define_echo!(42)` expands to
/// `fn echo() -> u32 { 42 }`.
///
/// The value of this macro is not the expansion itself but the fact that a
/// crate using it must be compiled — which makes rustc load this crate's
/// dylib at build time. That load is what regresses to
/// "E0463 can't find crate for echo_macro" when the toolchain's libstd is
/// not resolvable for proc-macro dylibs (see nix/packages.nix).
#[proc_macro]
pub fn define_echo(item: TokenStream) -> TokenStream {
    format!("fn echo() -> u32 {{ {item} }}").parse().unwrap()
}
