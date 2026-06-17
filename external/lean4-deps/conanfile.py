import os
from io import StringIO

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.files import copy

DEP_TARGETS = [
    "ProofWidgets:static",
    "ImportGraph:static",
    "LeanSearchClient:static",
    "Plausible:static",
    "Aesop:static",
    "Qq:static",
    "Batteries:static",
    "Mathlib:static",
]

MIN_OBJECTS = 7000


class Lean4Deps(ConanFile):
    """Prebuilt Lean4 dependency objects (mathlib + transitive deps).

    Downloads the mathlib olean cache and compiles the dependency :static
    objects (oleans + .c.o.export) once. It rebuilds when the mathlib pin changes,
    not when the model changes.
    """

    name = "lean4-deps"
    settings = "os", "arch"

    def set_version(self):
        if self.version is None:
            path = os.path.join(
                self.recipe_folder, "..", "..", "formal_verification", "lean-toolchain"
            )
            with open(path, encoding="utf-8") as f:
                self.version = f.read().strip().split(":v")[1]

    def export_sources(self):
        src = os.path.join(self.recipe_folder, "..", "..", "formal_verification")
        for f in ("lakefile.toml", "lake-manifest.json", "lean-toolchain"):
            copy(self, f, src=src, dst=self.export_sources_folder)

    def build_requirements(self):
        self.tool_requires(f"lean4/{self.version}")

    def build(self):
        # capture lake output and log only on failure.
        # lake :static archive throws ARG_MAX error on mathlib (harmless), so verify by count
        log = StringIO()
        try:
            self.run("lake exe cache get", cwd=self.build_folder, stdout=log, stderr=log)
            self.run(
                "lake build " + " ".join(DEP_TARGETS),
                cwd=self.build_folder,
                stdout=log,
                stderr=log,
                ignore_errors=True,
            )
            n = self._object_count()
            if n < MIN_OBJECTS:
                raise ConanException(
                    f"lean4-deps: Lean4 only {n} objects compiled (expected >= {MIN_OBJECTS})"
                )
        except Exception:
            self.output.error(log.getvalue())
            raise
        self.output.info(f"lean4-deps: Lean4 compiled {n} objects")

    def _object_count(self):
        pkgs = os.path.join(self.build_folder, ".lake", "packages")
        n = 0
        for root, _dirs, files in os.walk(pkgs):
            if "/ir/Cache/" not in (root.replace("\\", "/") + "/"):
                n += sum(1 for f in files if f.endswith(".c.o.export"))
        return n

    def package(self):
        copy(
            self,
            "*",
            src=os.path.join(self.build_folder, ".lake", "packages"),
            dst=os.path.join(self.package_folder, "packages"),
        )
