//! Screening a contract before it reaches the ledger.
//!
//! [`check`] answers whether [`crate::run`] would refuse a module before the
//! guest's first instruction — the three stages a caller maps to a malformed
//! transaction rather than to a failed one. It needs **no host, no store and no
//! gas**: everything it reads is a property of the compiled module. That is what
//! makes it callable from a transaction's preflight, which has no ledger to serve
//! host calls from.
//!
//! Two things it deliberately does not screen. A module exporting **no** linear
//! memory passes: a contract that makes no host call needs none, and one that
//! does is refused at the call and charged for what it burned. A start section
//! passes: it is guest code, and executing it is the one thing a check must not do
//! — a trap in one is charged to the contract like any other trap.
//!
//! One thing it screens that a run can only discover: an exported memory larger
//! than the engine grants. See [`check_memory`] for what stays invisible.

use std::fmt;
use wasmi::{ExternType, FuncType, Module, ValType};
use xrpl_host_functions::HostFunctionSpec;

use crate::register::HOST_MODULE;
use crate::vm::{MAX_MEMORY_PAGES, compile};

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
    /// The module asks for more linear memory than the engine grants.
    Memory(String),
}

impl fmt::Display for CheckError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CheckError::Compile(detail) => write!(f, "compile: {detail}"),
            CheckError::Import(detail) => write!(f, "import: {detail}"),
            // The detail says which of the entry point's failures this is, since
            // "no entry point" would be wrong for an export of the wrong type.
            CheckError::EntryPoint(detail) => write!(f, "{detail}"),
            CheckError::Memory(detail) => write!(f, "memory: {detail}"),
        }
    }
}

/// Screen `wasm`: it must compile, import only what the engine serves, export
/// `function_name` as `() -> i32`, and ask for no more memory than it may have.
///
/// The stages are ordered by how much of the module each explains. An import fault
/// is reported before a missing entry point because the imports are what the rest of
/// the module is built on; memory comes last, being a resource request rather than a
/// mistake about the ABI.
pub fn check(wasm: &[u8], function_name: &str) -> Result<(), CheckError> {
    let module = compile(wasm).map_err(CheckError::Compile)?;
    check_imports(&module)?;
    check_entry_point(&module, function_name)?;
    check_memory(&module)
}

/// Every import must be one the linker defines. The first that is not ends the
/// check, so a module with several faults reports the earliest.
fn check_imports(module: &Module) -> Result<(), CheckError> {
    for import in module.imports() {
        check_import(import.module(), import.name(), import.ty()).map_err(CheckError::Import)?;
    }
    Ok(())
}

/// Whether the engine defines this one import.
///
/// The set of names is [`HostFunctionSpec::ALL`], which is also what
/// [`crate::register::register_host_functions`] iterates — so a check and a run
/// cannot disagree about which names exist, and adding a host function extends
/// both at once. The one thing this does not compare is `ty`'s *signature*, which
/// still parts a module from the engine at instantiation; the kind is compared
/// because the engine defines these names as functions and as nothing else.
///
/// The rules are ordered, not merely alternatives: a guest importing `env::malloc`
/// is told about the namespace rather than that `malloc` is not a host function,
/// because the namespace is the one that explains every other import it has too.
fn check_import(module: &str, name: &str, ty: &ExternType) -> Result<(), String> {
    if module != HOST_MODULE {
        return Err(format!("'{module}::{name}' is not from '{HOST_MODULE}'"));
    }
    if !HostFunctionSpec::ALL
        .iter()
        .any(|op| op.wasm_name() == name)
    {
        return Err(format!("no host function '{name}'"));
    }
    if !matches!(ty, ExternType::Func(_)) {
        return Err(format!("'{HOST_MODULE}::{name}' is not a function"));
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

/// A module may not declare more linear memory than the engine grants.
///
/// Only what it *exports* is visible here. A memory a module keeps to itself is not
/// in its exports, and the store's limiter is what refuses that one — at
/// instantiation, where the run is charged nothing and the caller cannot tell it
/// from any other resource failure. Screening the exported case covers every
/// contract built against the guest SDK, since a contract needs an exported memory
/// to make a host call at all.
fn check_memory(module: &Module) -> Result<(), CheckError> {
    for export in module.exports() {
        if let ExternType::Memory(ty) = export.ty() {
            check_initial_pages(ty.minimum()).map_err(CheckError::Memory)?;
        }
    }
    Ok(())
}

/// Whether the engine will grant a memory of this declared initial size.
///
/// The *minimum* only: a declared maximum past the cap is legal and simply
/// unreachable, which `vm_limits::a_declared_maximum_past_the_cap_is_allowed_but_
/// unreachable` pins on the run side. Refusing it here would turn a runnable
/// contract away.
fn check_initial_pages(pages: u64) -> Result<(), String> {
    if pages > u64::from(MAX_MEMORY_PAGES) {
        return Err(format!(
            "initial memory of {pages} pages is past the {MAX_MEMORY_PAGES}-page cap"
        ));
    }
    Ok(())
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

/// The rules, one by one, on inputs built directly rather than parsed out of a
/// module. `tests/preflight.rs` runs real modules through [`check`]; what is here is
/// what a module cannot state precisely — which rule fires, in which order, and in
/// what words the caller logs it.
///
/// `wat` is a dev-dependency, so the one test here that does need a module writes it
/// as text like every other test in the crate. What the library must not gain is a
/// text *entry point* — `check` and `run` take binaries — and a `cfg(test)` caller
/// cannot give it one.
#[cfg(test)]
mod tests {
    use super::*;
    use wasmi::{GlobalType, MemoryType, Mutability};

    /// A host function as a guest declares it. Any function type will do: the
    /// signature is not what [`check_import`] compares.
    fn a_function() -> ExternType {
        ExternType::Func(FuncType::new([ValType::I32], [ValType::I32]))
    }

    /// A name every one of these tests can use, taken from the ABI rather than
    /// spelled, so it stays a real host function as the ABI changes.
    fn a_host_function_name() -> &'static str {
        HostFunctionSpec::ALL[0].wasm_name()
    }

    // -----------------------------------------------------------------------
    // Imports
    // -----------------------------------------------------------------------

    /// Every name the ABI declares is served. Derived from `ALL` rather than
    /// listed, so a host function added to the ABI is covered the day it lands.
    #[test]
    fn every_declared_host_function_is_served() {
        for op in HostFunctionSpec::ALL {
            assert_eq!(
                check_import(HOST_MODULE, op.wasm_name(), &a_function()),
                Ok(()),
                "{}",
                op.wasm_name()
            );
        }
    }

    #[test]
    fn an_import_from_another_namespace_is_refused() {
        for namespace in ["env", "host", "host_lib2", ""] {
            let refusal = check_import(namespace, a_host_function_name(), &a_function())
                .expect_err(namespace);
            assert!(
                refusal.contains("is not from 'host_lib'"),
                "{namespace}: {refusal}"
            );
        }
    }

    #[test]
    fn an_unknown_name_is_refused() {
        let refusal =
            check_import(HOST_MODULE, "no_such_function", &a_function()).expect_err("unknown name");
        assert_eq!(refusal, "no host function 'no_such_function'");
    }

    /// The engine defines these names as functions and as nothing else, so a module
    /// importing one as a global or a memory does not link either.
    #[test]
    fn a_host_function_imported_as_anything_else_is_refused() {
        for ty in [
            ExternType::Global(GlobalType::new(ValType::I32, Mutability::Const)),
            ExternType::Memory(MemoryType::new(1, None)),
        ] {
            let name = a_host_function_name();
            let refusal = check_import(HOST_MODULE, name, &ty).expect_err("not a function");
            assert_eq!(refusal, format!("'host_lib::{name}' is not a function"));
        }
    }

    /// The rules are ordered. An import that breaks two of them is reported by the
    /// first, so the message a contract author reads is the one that explains the
    /// rest of their imports too.
    #[test]
    fn the_namespace_is_reported_before_the_name() {
        let refusal = check_import("env", "no_such_function", &a_function())
            .expect_err("neither the namespace nor the name is served");

        assert!(refusal.contains("is not from 'host_lib'"), "{refusal}");
        assert!(
            !refusal.contains("no host function"),
            "the namespace explains it: {refusal}"
        );
    }

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

    /// Three faults, three descriptions. A run reports these too, with wasmi's own
    /// error appended, so a swapped arm would mislead at both stages at once.
    #[test]
    fn each_entry_point_fault_is_described_as_itself() {
        assert_eq!(
            entry_point_fault(Some(a_function()), "finish"),
            "entry point 'finish' has the wrong signature, expected '() -> i32'"
        );
        assert_eq!(
            entry_point_fault(
                Some(ExternType::Global(GlobalType::new(
                    ValType::I32,
                    Mutability::Const
                ))),
                "finish"
            ),
            "export 'finish' is not a function"
        );
        assert_eq!(
            entry_point_fault(None, "finish"),
            "no entry point 'finish'",
            "an absent export must not be reported as a wrong signature"
        );
    }

    /// The cap itself is granted; one page past it is not. The boundary is the whole
    /// rule, and it is the same boundary the store's limiter applies at
    /// instantiation.
    #[test]
    fn the_initial_memory_may_reach_the_cap_but_not_pass_it() {
        assert_eq!(check_initial_pages(0), Ok(()));
        assert_eq!(check_initial_pages(u64::from(MAX_MEMORY_PAGES)), Ok(()));

        let past = u64::from(MAX_MEMORY_PAGES) + 1;
        let refusal = check_initial_pages(past).expect_err("one page past the cap");
        assert_eq!(
            refusal,
            format!("initial memory of {past} pages is past the {MAX_MEMORY_PAGES}-page cap")
        );
    }

    /// The bridge logs this string and the C++ tests match on it, so the stage's
    /// prefix is part of the interface rather than a debugging aid.
    #[test]
    fn a_refusal_names_its_stage() {
        assert_eq!(
            CheckError::Compile("bad magic".to_string()).to_string(),
            "compile: bad magic"
        );
        assert_eq!(
            CheckError::Memory("initial memory of 129 pages".to_string()).to_string(),
            "memory: initial memory of 129 pages"
        );
        assert_eq!(
            CheckError::Import("no host function 'x'".to_string()).to_string(),
            "import: no host function 'x'"
        );
        // The entry point's detail already says which of its three faults it is,
        // so a prefix would only repeat it.
        assert_eq!(
            CheckError::EntryPoint("no entry point 'finish'".to_string()).to_string(),
            "no entry point 'finish'"
        );
    }

    #[test]
    fn the_stages_run_in_order() {
        assert!(
            matches!(check(b"not wasm", "finish"), Err(CheckError::Compile(_))),
            "nothing is screened until the module compiles"
        );

        // A module that compiles and imports nothing, so it reaches the entry point.
        let empty = wat::parse_str("(module)").expect("assembles");
        assert!(
            matches!(check(&empty, "finish"), Err(CheckError::EntryPoint(_))),
            "a module that compiles and imports nothing reaches the entry point"
        );
    }
}
