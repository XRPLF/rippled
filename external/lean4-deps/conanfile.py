import os
import shutil
from io import StringIO
from pathlib import Path

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

PACKAGES_DIR = "packages"
LIB_DIR = "lib"
ARCHIVE_NAME = "libLeanDeps.a"


class Lean4Deps(ConanFile):
    """Prebuilt Lean4 mathlib and transitive deps bundled into lib/libLeanDeps.a.

    Downloads the mathlib olean cache and compiles the dependency :static
    objects (oleans + .c.o.export) once. It rebuilds when the mathlib pin changes,
    not when the model changes.
    """

    name = "lean4-deps"
    settings = "os", "arch"

    def set_version(self):
        if self.version is None:
            toolchain = Path(
                self.recipe_folder, "..", "..", "formal_verification", "lean-toolchain"
            )
            self.version = toolchain.read_text(encoding="utf-8").strip().split(":v")[1]

    def export_sources(self):
        source_dir = Path(self.recipe_folder, "..", "..", "formal_verification")
        for filename in ("lakefile.toml", "lake-manifest.json", "lean-toolchain"):
            copy(self, filename, src=source_dir, dst=self.export_sources_folder)

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
            objects = self._dep_objects()
            n = len(objects)
            if n < MIN_OBJECTS:
                raise ConanException(
                    f"lean4-deps: Lean4 only {n} objects compiled (expected >= {MIN_OBJECTS})"
                )
            self._bundle_deps(objects)
        except Exception:
            self.output.error(log.getvalue())
            raise
        self.output.info(
            f"lean4-deps: Lean4 compiled {n} objects, bundled into {ARCHIVE_NAME}"
        )

    def _dep_objects(self):
        # Native objects from `lake build :static` (cache get only fetches .olean/.c)
        packages_dir = Path(self.build_folder) / ".lake" / PACKAGES_DIR
        objects = []
        for dirpath, _dirs, filenames in os.walk(packages_dir):
            if "/ir/Cache/" in (dirpath.replace("\\", "/") + "/"):
                continue
            objects += [Path(dirpath) / name for name in filenames if name.endswith(".c.o.export")]
        return objects

    def _bundle_deps(self, objects):
        # short-named symlinks let the toolchain's llvm-ar bundle 8000+ objects (cap error on long names)
        # @file avoids ARG_MAX (command line too long) error.
        build_dir = Path(self.build_folder)
        symlink_dir = build_dir / "lean_deps_symlinks"
        shutil.rmtree(symlink_dir, ignore_errors=True)
        symlink_dir.mkdir()

        symlinks = [symlink_dir / f"obj{i}.o" for i in range(len(objects))]
        for symlink, target in zip(symlinks, objects):
            symlink.symlink_to(target)

        response_file = build_dir / "lean_deps_objects.rsp"
        response_file.write_text(
            "".join(f"{symlink}\n" for symlink in symlinks), encoding="utf-8"
        )

        archive = build_dir / ARCHIVE_NAME
        self.run(f'llvm-ar qcs "{archive}" "@{response_file}"')

    def package(self):
        build_dir = Path(self.build_folder)
        package_dir = Path(self.package_folder)
        # Copy the whole lake build tree (.olean, .c files). Exclude .c.o.export files
        # as those are bundled into libLeanDeps.a and the model build never requests them
        copy(
            self,
            "*",
            src=build_dir / ".lake" / PACKAGES_DIR,
            dst=package_dir / PACKAGES_DIR,
            excludes=["*.c.o.export"],
        )
        copy(self, ARCHIVE_NAME, src=build_dir, dst=package_dir / LIB_DIR)

    def package_info(self):
        package_dir = Path(self.package_folder)
        self.cpp_info.set_property("packages", str(package_dir / PACKAGES_DIR))
        self.cpp_info.set_property("archive", str(package_dir / LIB_DIR / ARCHIVE_NAME))
