//! Screening a contract before it reaches the ledger.
//!
//! [`check`] answers whether [`crate::run`] would refuse a module before the
//! guest's first instruction — the three stages a caller maps to a malformed
//! transaction rather than to a failed one. It needs **no host, no store and no
//! gas**: everything it reads is a property of the compiled module. That is what
//! makes it callable from a transaction's preflight, which has no ledger to serve
//! host calls from.
//!
//! Two things it deliberately does not screen. A module exporting no linear
//! memory passes: a contract that makes no host call needs none, and one that
//! does is refused at the call and charged for what it burned. A start section
//! passes: it is guest code, and executing it is the one thing a check must not
//! do.

use std::fmt;
use wasmi::{ExternType, FuncType, Module, ValType};
use xrpl_host_functions::HostFunctionSpec;

use crate::register::HOST_MODULE;
use crate::vm::compile;

/// Why a module cannot be run. One variant per stage, since the caller maps the
/// stages separately.
#[derive(Debug)]
pub enum CheckError {
    /// `wasm` is not a valid module under this engine's configuration.
    Compile(String),
    /// An import no engine of this ABI defines: another module namespace, a name
    /// that is not a host function, or one imported as something other than a
    /// function.
    Import(String),
    /// No export named `function_name` with signature `() -> i32`.
    EntryPoint(String),
}

impl fmt::Display for CheckError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CheckError::Compile(detail) => write!(f, "compile: {detail}"),
            CheckError::Import(detail) => write!(f, "import: {detail}"),
            // The detail says which of the entry point's failures this is, since
            // "no entry point" would be wrong for an export of the wrong type.
            CheckError::EntryPoint(detail) => write!(f, "{detail}"),
        }
    }
}

/// Screen `wasm`: it must compile, import only what the engine serves, and export
/// `function_name` as `() -> i32`.
pub fn check(wasm: &[u8], function_name: &str) -> Result<(), CheckError> {
    let module = compile(wasm).map_err(CheckError::Compile)?;
    check_imports(&module)?;
    check_entry_point(&module, function_name)
}

/// Every import must be one the linker defines.
///
/// The set is [`HostFunctionSpec::ALL`], which is also what
/// [`crate::register::register_host_functions`] iterates — so a check and a run
/// cannot disagree about which names exist, and adding a host function extends
/// both at once. The one thing this does not compare is the *signature*, which
/// still parts a module from the engine at instantiation.
fn check_imports(module: &Module) -> Result<(), CheckError> {
    for import in module.imports() {
        let name = import.name();

        if import.module() != HOST_MODULE {
            return Err(CheckError::Import(format!(
                "'{}::{name}' is not from '{HOST_MODULE}'",
                import.module()
            )));
        }
        if !HostFunctionSpec::ALL
            .iter()
            .any(|op| op.wasm_name() == name)
        {
            return Err(CheckError::Import(format!("no host function '{name}'")));
        }
        if !matches!(import.ty(), ExternType::Func(_)) {
            return Err(CheckError::Import(format!(
                "'{HOST_MODULE}::{name}' is not a function"
            )));
        }
    }
    Ok(())
}

fn check_entry_point(module: &Module, name: &str) -> Result<(), CheckError> {
    match module.get_export(name) {
        Some(ExternType::Func(ty)) if is_entry_point(&ty) => Ok(()),
        found => Err(CheckError::EntryPoint(entry_point_fault(found, name))),
    }
}

/// The entry point's type: nothing in, one `i32` out — what [`crate::run`]'s
/// `get_typed_func::<(), i32>` accepts.
fn is_entry_point(ty: &FuncType) -> bool {
    ty.params().is_empty() && matches!(ty.results(), [ValType::I32])
}

/// How an entry-point lookup failed, in the words both stages use: a check and a
/// run describe the same module the same way, and "no entry point" would send a
/// contract author looking for a function they already have.
pub(crate) fn entry_point_fault(found: Option<ExternType>, name: &str) -> String {
    match found {
        Some(ExternType::Func(_)) => {
            format!("entry point '{name}' has the wrong signature, expected '() -> i32'")
        }
        Some(_) => format!("export '{name}' is not a function"),
        None => format!("no entry point '{name}'"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Both halves of the type are load-bearing, and neither is checked anywhere
    /// a module cannot reach.
    #[test]
    fn the_entry_point_type_is_nothing_in_and_one_i32_out() {
        assert!(is_entry_point(&FuncType::new([], [ValType::I32])));

        for wrong in [
            FuncType::new([], []),
            FuncType::new([], [ValType::I64]),
            FuncType::new([ValType::I32], [ValType::I32]),
            FuncType::new([], [ValType::I32, ValType::I32]),
        ] {
            assert!(!is_entry_point(&wrong), "{wrong:?}");
        }
    }
}
