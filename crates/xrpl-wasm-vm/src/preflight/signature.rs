//! The last rule on an import: an import that names a host function must also
//! declare the type the engine registers it as.
//!
//! A file of its own because it is the one import rule with machinery to carry:
//! the ABI's derived signature, its map into wasmi's value types, and a rendering
//! of a function type for the refusal.
//!
//! **Arity and value types are the whole of it** — an `i64` in an `i32`'s place, or
//! a `u32` parameter read as one wasm parameter rather than two. **Parameter order
//! is invisible**: nearly everything lowers to `i32`, so two swapped parameters of
//! the same type leave the function type identical.

use wasmi::{FuncType, ValType};
use xrpl_host_functions::{HostFunctionSpec, WasmValType};

/// Whether this import declares the type the engine registers — one that does not
/// is what a module parts from the linker over at instantiation.
pub(super) fn check_signature(
    function: HostFunctionSpec,
    imported: &FuncType,
) -> Result<(), String> {
    let registered = registered_type(function);
    if *imported == registered {
        return Ok(());
    }
    Err(format!(
        "'{}' expected '{}', found '{}'",
        function.wasm_name(),
        signature(&registered),
        signature(imported)
    ))
}

/// The type the engine registers `function` as: the wasm signature derived from its
/// declaration, in wasmi's own vocabulary.
///
/// Building one to compare against costs nothing — `FuncType` holds up to 21 value
/// types inline on a 64-bit target and the ABI's widest signature is nine, so this
/// is a stack value and the comparison above is one `==`.
pub(super) fn registered_type(function: HostFunctionSpec) -> FuncType {
    FuncType::new(
        function.wasm_params().iter().copied().map(val_type),
        function.wasm_result().map(val_type),
    )
}

/// The one place the ABI's value types become the engine's.
fn val_type(declared: WasmValType) -> ValType {
    match declared {
        WasmValType::I32 => ValType::I32,
        WasmValType::I64 => ValType::I64,
    }
}

/// A function type as `(i32, i32) -> i32`, and as `(i32, i32)` for a function
/// answering nothing — the spelling [`super::entry_point_fault`] uses, so the two
/// stages describe a signature the same way.
fn signature(ty: &FuncType) -> String {
    let params = to_string(ty.params());
    match ty.results() {
        [] => format!("({params})"),
        results => format!("({params}) -> {}", to_string(results)),
    }
}

/// The types of one position, as a signature lists them.
fn to_string(types: &[ValType]) -> String {
    types
        .iter()
        .copied()
        .map(as_str)
        .collect::<Vec<_>>()
        .join(", ")
}

/// A wasm value type as the text format spells it. Total over [`ValType`] because a
/// refusal renders the found side too, which is whatever the module declared.
fn as_str(val_type: ValType) -> &'static str {
    match val_type {
        ValType::I32 => "i32",
        ValType::I64 => "i64",
        ValType::F32 => "f32",
        ValType::F64 => "f64",
        ValType::V128 => "v128",
        ValType::FuncRef => "funcref",
        ValType::ExternRef => "externref",
    }
}

/// The rule and the derivation under it, on function types built directly. Which
/// `CheckError` a refusal becomes and where this rule sits among the other three
/// are the parent's tests.
#[cfg(test)]
mod tests {
    use super::*;

    /// The three ways an import's type can differ from the one registered, each
    /// against a real declaration: a wrong value type, a wrong arity, and a result
    /// where the ABI answers nothing.
    ///
    /// The arity case is the one that matters in practice — a guest that reads
    /// `check_keylet`'s `seq: u32` as a scalar writes exactly that signature.
    #[test]
    fn an_import_of_the_wrong_type_is_refused() {
        let refusal = check_signature(
            HostFunctionSpec::GetLedgerSqn,
            &FuncType::new([ValType::I64, ValType::I64], [ValType::I32]),
        )
        .expect_err("i64 where i32 belongs");
        assert_eq!(
            refusal,
            "'ldgr_index' expected '(i32, i32) -> i32', found '(i64, i64) -> i32'"
        );

        let refusal = check_signature(
            HostFunctionSpec::CheckKeylet,
            &FuncType::new([ValType::I32; 5], [ValType::I32]),
        )
        .expect_err("a u32 read as one parameter rather than two");
        assert_eq!(
            refusal,
            "'check_id' expected '(i32, i32, i32, i32, i32, i32) -> i32', \
             found '(i32, i32, i32, i32, i32) -> i32'"
        );

        let refusal = check_signature(
            HostFunctionSpec::Trace,
            &FuncType::new([ValType::I32; 5], [ValType::I32]),
        )
        .expect_err("a result from the one function that answers nothing");
        assert_eq!(
            refusal,
            "'trace' expected '(i32, i32, i32, i32, i32)', \
             found '(i32, i32, i32, i32, i32) -> i32'"
        );
    }

    /// Both of the ABI's value types survive the map to the engine's vocabulary:
    /// an `i64` collapsed to an `i32` would make the check accept what the linker
    /// refuses, and a result invented for `trace` would make it refuse what the
    /// linker accepts.
    #[test]
    fn the_derived_type_keeps_i64_and_the_absent_result() {
        assert_eq!(
            signature(&registered_type(HostFunctionSpec::FloatFromInt)),
            "(i64, i32, i32, i32) -> i32"
        );
        assert_eq!(
            signature(&registered_type(HostFunctionSpec::Trace)),
            "(i32, i32, i32, i32, i32)"
        );
    }
}
