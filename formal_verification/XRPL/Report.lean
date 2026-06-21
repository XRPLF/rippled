import XRPL.Properties.Properties
import Lean

open Lean Elab Command PrettyPrinter

/-- A "headline" module: under `XRPL.Properties.Protocol.`, and not a `*.Common.*` proof body. -/
def isHeadline (m : Name) : Bool :=
  let s := m.toString
  s.startsWith "XRPL.Properties.Protocol." && (s.splitOn ".Common.").length == 1

run_cmd do
  let env ← getEnv
  let mods := env.header.moduleNames
  -- (module, theorem-name) for every theorem declared in a headline module
  let mut entries : Array (Name × Name) := #[]
  for (name, info) in env.constants.toList do
    if info.isThm && !name.isInternalDetail then
      if let some idx := env.getModuleIdxFor? name then
        if isHeadline mods[idx]! then
          entries := entries.push (mods[idx]!, name)
  let sorted := entries.qsort fun a b =>
    a.1.toString < b.1.toString || (a.1 == b.1 && a.2.toString < b.2.toString)
  let mut out := s!"# Theorem catalog\n\n{sorted.size} headline theorems.\n"
  let mut curMod : Name := .anonymous
  for (m, name) in sorted do
    if m != curMod then
      out := out ++ s!"\n## {m}\n\n"
      curMod := m
    let sig ← liftTermElabM (ppSignature name)
    out := out ++ s!"### {name}\n\n"
    if let some doc ← findDocString? env name then
      out := out ++ (doc.trimAscii.toString) ++ "\n\n"
    out := out ++ "```lean\n" ++ sig.fmt.pretty ++ "\n```\n\n"
  IO.FS.writeFile "THEOREM_CATALOG.md" out
  logInfo s!"wrote {entries.size} theorems to THEOREM_CATALOG.md"
