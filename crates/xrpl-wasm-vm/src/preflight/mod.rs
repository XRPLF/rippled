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
//! Two things it screens that a run can only discover: an exported memory, or an
//! exported table, larger than the engine grants. Both read the same export list, so
//! [`check_exported_resources`] is one pass — see it for what stays invisible, and
//! why the table case leaves much more of it there.
//!
//! Every rule is here but one: [`signature`] holds the comparison of an import's
//! type against the ABI's, which needs the ABI's value-type vocabulary and a
//! rendering of a function type on top of what the rest of the stage reads.

mod signature;

use std::fmt;
use wasmi::{ExternType, FuncType, Module, ValType};
use xrpl_host_functions::{HOST_MODULE, HostFunctionSpec};

use crate::vm::{MAX_MEMORY_PAGES, MAX_TABLE_ELEMENTS, compile};
use signature::check_signature;

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
    /// An import of a host function typed as something other than what the engine
    /// registers it as. Apart from [`CheckError::Import`] because the ABI does have
    /// the function the guest asked for: what is wrong is the signature, not the
    /// name.
    Signature(String),
    /// No export named `function_name` with signature `() -> i32`.
    EntryPoint(String),
    /// The module asks for more linear memory than the engine grants.
    Memory(String),
    /// The module asks for a larger table than the engine grants.
    Table(String),
}

impl fmt::Display for CheckError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CheckError::Compile(detail) => write!(f, "compile: {detail}"),
            CheckError::Import(detail) => write!(f, "import: {detail}"),
            CheckError::Signature(detail) => write!(f, "signature: {detail}"),
            // The detail says which of the entry point's failures this is, since
            // "no entry point" would be wrong for an export of the wrong type.
            CheckError::EntryPoint(detail) => write!(f, "{detail}"),
            CheckError::Memory(detail) => write!(f, "memory: {detail}"),
            CheckError::Table(detail) => write!(f, "table: {detail}"),
        }
    }
}

/// Screen `wasm`: it must compile, import only what the engine serves, export
/// `function_name` as `() -> i32`, and ask for no more memory or table than it may
/// have.
///
/// The stages are ordered by how much of the module each explains. An import fault
/// is reported before a missing entry point because the imports are what the rest of
/// the module is built on; the resource caps come last, being a request rather than a
/// mistake about the ABI.
pub fn check(wasm: &[u8], function_name: &str) -> Result<(), CheckError> {
    let module = compile(wasm).map_err(CheckError::Compile)?;
    check_imports(&module)?;
    check_entry_point(&module, function_name)?;
    check_exported_resources(&module)
}

/// Every import must be one the linker defines, with the type it defines it as. The
/// first that is not ends the check, so a module with several faults reports the
/// earliest.
fn check_imports(module: &Module) -> Result<(), CheckError> {
    for import in module.imports() {
        check_import(import.module(), import.name(), import.ty())?;
    }
    Ok(())
}

/// Whether the engine defines this one import, as the guest declares it.
///
/// The names are [`HostFunctionSpec::ALL`] and the signatures are derived from the
/// same declarations, which is also what
/// [`crate::register::register_host_functions`] registers — so a check and a run
/// cannot disagree about which imports exist or what they look like, and adding a
/// host function extends both at once.
///
/// The rules are ordered, and each presumes the ones before it held. A guest
/// importing `env::malloc` is told about the namespace rather than that `malloc` is
/// not a host function, because the namespace is the one that explains every other
/// import it has too; and only an import that names a real host function as a
/// function has a signature worth comparing, which is why that rule is last.
fn check_import(module: &str, name: &str, ty: &ExternType) -> Result<(), CheckError> {
    let function = host_function(module, name).map_err(CheckError::Import)?;
    let imported = imported_function(name, ty).map_err(CheckError::Import)?;
    check_signature(function, imported).map_err(CheckError::Signature)
}

/// Which host function this import names: its namespace must be the engine's, and
/// its name one the ABI declares.
fn host_function(module: &str, name: &str) -> Result<HostFunctionSpec, String> {
    if module != HOST_MODULE {
        return Err(format!("'{module}::{name}' is not from '{HOST_MODULE}'"));
    }
    HostFunctionSpec::ALL
        .iter()
        .find(|function| function.wasm_name() == name)
        .copied()
        .ok_or_else(|| format!("no host function '{name}'"))
}

/// The function type the guest declared. The engine defines these names as functions
/// and as nothing else, so an import of any other kind does not link.
fn imported_function<'ty>(name: &str, ty: &'ty ExternType) -> Result<&'ty FuncType, String> {
    match ty {
        ExternType::Func(ty) => Ok(ty),
        _ => Err(format!("'{HOST_MODULE}::{name}' is not a function")),
    }
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

/// A module may declare no more linear memory, and no larger a table, than the
/// engine grants. One pass over the exports, since both rules read the same list and
/// the export table is the only place either is visible.
///
/// **A memory or table the module keeps to itself is therefore not screened**: it is
/// absent from the exports, and the store's limiter is what refuses it, at
/// instantiation. That gap is wide for tables — Rust exports
/// `__indirect_function_table` only under `--export-table`, so unexported is the
/// normal shape — and narrow for memories, since a contract needs an exported one to
/// make any host call at all.
///
/// A module faulting on both is reported by whichever it declares first. Neither
/// fault explains the other, so there is no precedence to preserve — only the need
/// for every node to reach the same verdict, which export order already gives.
fn check_exported_resources(module: &Module) -> Result<(), CheckError> {
    for export in module.exports() {
        match export.ty() {
            ExternType::Memory(ty) => {
                check_initial_pages(ty.minimum()).map_err(CheckError::Memory)?;
            }
            ExternType::Table(ty) => {
                check_initial_elements(ty.minimum()).map_err(CheckError::Table)?;
            }
            _ => {}
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

/// Whether the engine will grant a table of this declared initial size.
///
/// The *minimum* is the whole question: `table.grow` belongs to the reference-types
/// proposal, which [`crate::vm`]'s engine turns off, so a table never becomes larger
/// than it was declared and a declared maximum past the cap is simply unreachable.
fn check_initial_elements(elements: u64) -> Result<(), String> {
    let cap = u64::try_from(MAX_TABLE_ELEMENTS).expect("the cap is a small constant");
    if elements > cap {
        return Err(format!(
            "initial table of {elements} elements is past the {MAX_TABLE_ELEMENTS}-element cap"
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
/// what words the caller logs it. The signature rule's own derivation is tested
/// beside it, in [`signature`].
///
/// `wat` is a dev-dependency, so the one test here that does need a module writes it
/// as text like every other test in the crate. What the library must not gain is a
/// text *entry point* — `check` and `run` take binaries — and a `cfg(test)` caller
/// cannot give it one.
#[cfg(test)]
mod tests {
    use super::signature::registered_type;
    use super::*;
    use wasmi::{GlobalType, MemoryType, Mutability};

    /// The type an import of `function` must declare: what the engine registers it
    /// as.
    fn registered(function: HostFunctionSpec) -> ExternType {
        ExternType::Func(registered_type(function))
    }

    /// A name every one of these tests can use, taken from the ABI rather than
    /// spelled, so it stays a real host function as the ABI changes.
    fn a_host_function_name() -> &'static str {
        HostFunctionSpec::ALL[0].wasm_name()
    }

    // -----------------------------------------------------------------------
    // Imports
    // -----------------------------------------------------------------------

    /// Every name the ABI declares is served, at the signature derived from its
    /// declaration. Derived from `ALL` rather than listed, so a host function added
    /// to the ABI is covered the day it lands.
    ///
    /// Both sides of the comparison come from the table, so what this pins is that
    /// no declaration is refused — not that the table is right. Comparing it against
    /// an independent statement of the wire is
    /// `tests/preflight.rs::every_declared_host_function_may_be_imported`, whose 60
    /// import declarations are hand-written, and
    /// `the_derived_signatures_are_what_the_linker_registers` beside it, which puts
    /// the table through the real linker.
    #[test]
    fn every_declared_host_function_is_served() {
        for &function in HostFunctionSpec::ALL {
            let name = function.wasm_name();
            if let Err(refusal) = check_import(HOST_MODULE, name, &registered(function)) {
                panic!("'{name}' is declared but not served: {refusal}");
            }
        }
    }

    #[test]
    fn an_import_from_another_namespace_is_refused() {
        for namespace in ["env", "host", "host_lib2", ""] {
            let refusal = host_function(namespace, a_host_function_name()).expect_err(namespace);
            assert!(
                refusal.contains("is not from 'host_lib'"),
                "{namespace}: {refusal}"
            );
        }
    }

    #[test]
    fn an_unknown_name_is_refused() {
        let refusal = host_function(HOST_MODULE, "no_such_function").expect_err("unknown name");
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
            let refusal = imported_function(name, &ty).expect_err("not a function");
            assert_eq!(refusal, format!("'host_lib::{name}' is not a function"));
        }
    }

    /// The rules are ordered. An import that breaks two of them is reported by the
    /// first, so the message a contract author reads is the one that explains the
    /// rest of their imports too.
    #[test]
    fn the_namespace_is_reported_before_the_name() {
        let refusal = host_function("env", "no_such_function")
            .expect_err("neither the namespace nor the name is served");

        assert!(refusal.contains("is not from 'host_lib'"), "{refusal}");
        assert!(
            !refusal.contains("no host function"),
            "the namespace explains it: {refusal}"
        );
    }

    /// The signature is the last rule, so an import wrong about the namespace, the
    /// name or the kind is not also told about a signature it was never going to
    /// reach — and the two kinds of fault reach the caller as different stages.
    #[test]
    fn the_signature_is_the_last_rule() {
        let name = a_host_function_name();
        let mistyped = ExternType::Func(FuncType::new([ValType::F32], []));
        let not_a_function = ExternType::Global(GlobalType::new(ValType::I32, Mutability::Const));

        for (rule, refusal) in [
            ("the namespace", check_import("env", name, &mistyped)),
            (
                "the name",
                check_import(HOST_MODULE, "no_such_function", &mistyped),
            ),
            ("the kind", check_import(HOST_MODULE, name, &not_a_function)),
        ] {
            assert!(
                matches!(refusal, Err(CheckError::Import(_))),
                "{rule} explains this import, not its signature: {refusal:?}"
            );
        }

        assert!(
            matches!(
                check_import(HOST_MODULE, name, &mistyped),
                Err(CheckError::Signature(_))
            ),
            "an import that breaks nothing but the signature is a signature fault"
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
            entry_point_fault(
                Some(ExternType::Func(FuncType::new(
                    [ValType::I32],
                    [ValType::I32]
                ))),
                "finish"
            ),
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

    /// The cap itself is granted; one element past it is not. The boundary is the
    /// whole rule, and it is the same boundary the store's limiter applies at
    /// instantiation.
    #[test]
    fn the_initial_table_may_reach_the_cap_but_not_pass_it() {
        let cap = u64::try_from(MAX_TABLE_ELEMENTS).expect("fits");
        assert_eq!(check_initial_elements(0), Ok(()));
        assert_eq!(check_initial_elements(cap), Ok(()));

        let past = cap + 1;
        let refusal = check_initial_elements(past).expect_err("one element past the cap");
        assert_eq!(
            refusal,
            format!(
                "initial table of {past} elements is past the {MAX_TABLE_ELEMENTS}-element cap"
            )
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
            CheckError::Table("initial table of 1025 elements".to_string()).to_string(),
            "table: initial table of 1025 elements"
        );
        assert_eq!(
            CheckError::Import("no host function 'x'".to_string()).to_string(),
            "import: no host function 'x'"
        );
        // The stage is the prefix and nothing else, so the detail must not say
        // "signature" a second time.
        assert_eq!(
            CheckError::Signature(
                "'ldgr_index' expected '(i32, i32) -> i32', found '(i64, i64) -> i32'".to_string()
            )
            .to_string(),
            "signature: 'ldgr_index' expected '(i32, i32) -> i32', found '(i64, i64) -> i32'"
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
