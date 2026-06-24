from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get

# sha256-pinned Lean releases per platform (no elan installer) (bump with the lean-toolchain pin)
_SHA256 = {
    ("Macos", "x86_64"): "47010e6040ab2441dc96c1d9a3aca1721576fdbe4da566d938b29a26502fd378",
    ("Macos", "armv8"): "d63a34d12978b035f871c8448d7243eb16711b8f5b27d7e9b093a210c1117e8d",
    ("Linux", "x86_64"): "b02b74bb23e93e5b05f03f51ad06274814337d107718a02b6f89dc4db1387416",
    ("Linux", "armv8"): "c608141afb645c7faa3845cc5dc503890ae329a82359f9bf37358d1fab499f81",
}

RELEASE_URL = "https://github.com/leanprover/lean4/releases/download/v{version}/lean-{version}-{os_tag}{arch_suffix}.zip"


class Lean(ConanFile):
    """Lean 4 toolchain (lean, lake, headers, runtime) from the pinned leanprover/lean4 release.

    Version defaults from formal_verification/lean-toolchain, pass --version
    when exporting outside the repo layout (e.g. the CI image).
    """

    name = "lean4"
    license = "Apache-2.0"
    url = "https://github.com/leanprover/lean4"
    description = "The Lean 4 theorem prover and toolchain"
    settings = "os", "arch"

    def set_version(self):
        if self.version is None:
            toolchain = Path(
                self.recipe_folder, "..", "..", "formal_verification", "lean-toolchain"
            )
            # "leanprover/lean4:vX" -> "X"
            self.version = toolchain.read_text(encoding="utf-8").strip().split(":v")[1]

    def build(self):
        os_name, arch = str(self.settings.os), str(self.settings.arch)
        sha256 = _SHA256.get((os_name, arch))
        if sha256 is None:
            raise ConanInvalidConfiguration(f"lean4: unsupported platform {os_name}/{arch}")
        os_tag = "darwin" if os_name == "Macos" else "linux"
        arch_suffix = "_aarch64" if arch == "armv8" else ""
        url = RELEASE_URL.format(
            version=self.version, os_tag=os_tag, arch_suffix=arch_suffix
        )
        get(
            self,
            url,
            sha256=sha256,
            strip_root=True,
            keep_permissions=True,
            destination=Path(self.build_folder) / "toolchain",
        )

    def package(self):
        copy(
            self,
            "*",
            src=Path(self.build_folder) / "toolchain",
            dst=self.package_folder,
        )

    def package_info(self):
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libdirs = [str(Path("lib") / "lean")]
        self.cpp_info.libs = ["Lake", "leanshared"]  # order matters: Lake before the runtime
        self.cpp_info.bindirs = ["bin"]
        self.buildenv_info.prepend_path("PATH", str(Path(self.package_folder) / "bin"))
