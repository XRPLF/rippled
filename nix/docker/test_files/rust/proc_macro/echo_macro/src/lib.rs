use proc_macro::TokenStream;

#[proc_macro]
pub fn define_echo(item: TokenStream) -> TokenStream {
    format!("fn echo() -> u32 {{ {item} }}").parse().unwrap()
}
