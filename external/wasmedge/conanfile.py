from conan import ConanFile
from conan.tools.files import get, copy, download
from conan.tools.scm import Version
from conan.errors import ConanInvalidConfiguration

import os

required_conan_version = ">=1.53.0"

class WasmedgeConan(ConanFile):
    name = "wasmedge"
    description = ("WasmEdge is a lightweight, high-performance, and extensible WebAssembly runtime"
                "for cloud native, edge, and decentralized applications."
                "It powers serverless apps, embedded functions, microservices, smart contracts, and IoT devices.")
    license = "Apache-2.0"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/WasmEdge/WasmEdge/"
    topics = ("webassembly", "wasm", "wasi", "emscripten")
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"

    @property
    def _compiler_alias(self):
        return {
            "Visual Studio": "Visual Studio",
            # "Visual Studio": "msvc",
            "msvc": "msvc",
        }.get(str(self.info.settings.compiler), "gcc")

    def configure(self):
        self.settings.compiler.rm_safe("libcxx")
        self.settings.compiler.rm_safe("cppstd")

    def validate(self):
        try:
            self.conan_data["sources"][self.version][str(self.settings.os)][str(self.settings.arch)][self._compiler_alias]
        except KeyError:
            raise ConanInvalidConfiguration("Binaries for this combination of version/os/arch/compiler are not available")

    def package_id(self):
        del self.info.settings.compiler.version
        self.info.settings.compiler = self._compiler_alias

    def build(self):
        # This is packaging binaries so the download needs to be in build
        get(self, **self.conan_data["sources"][self.version][str(self.settings.os)][str(self.settings.arch)][self._compiler_alias][0],
            destination=self.source_folder, strip_root=True)
        download(self, filename="LICENSE",
                 **self.conan_data["sources"][self.version][str(self.settings.os)][str(self.settings.arch)][self._compiler_alias][1])

    def package(self):
        copy(self, pattern="*.h", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder, "include"), keep_path=True)
        copy(self, pattern="*.inc", dst=os.path.join(self.package_folder, "include"), src=os.path.join(self.source_folder, "include"), keep_path=True)

        srclibdir = os.path.join(self.source_folder, "lib64" if self.settings.os == "Linux" else "lib")
        srcbindir = os.path.join(self.source_folder, "bin")
        dstlibdir = os.path.join(self.package_folder, "lib")
        dstbindir = os.path.join(self.package_folder, "bin")
        if Version(self.version) >= "0.11.1":
            copy(self, pattern="wasmedge.lib", src=srclibdir, dst=dstlibdir, keep_path=False)
            copy(self, pattern="wasmedge.dll", src=srcbindir, dst=dstbindir, keep_path=False)
            copy(self, pattern="libwasmedge.so*", src=srclibdir, dst=dstlibdir, keep_path=False)
            copy(self, pattern="libwasmedge*.dylib", src=srclibdir,  dst=dstlibdir, keep_path=False)
        else:
            copy(self, pattern="wasmedge_c.lib", src=srclibdir, dst=dstlibdir, keep_path=False)
            copy(self, pattern="wasmedge_c.dll", src=srcbindir, dst=dstbindir, keep_path=False)
            copy(self, pattern="libwasmedge_c.so*", src=srclibdir, dst=dstlibdir, keep_path=False)
            copy(self, pattern="libwasmedge_c*.dylib", src=srclibdir,  dst=dstlibdir, keep_path=False)

        copy(self, pattern="wasmedge*", src=srcbindir, dst=dstbindir, keep_path=False)
        copy(self, pattern="LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"), keep_path=False)

    def package_info(self):
        if Version(self.version) >= "0.11.1":
            self.cpp_info.libs = ["wasmedge"]
        else:
            self.cpp_info.libs = ["wasmedge_c"]

        bindir = os.path.join(self.package_folder, "bin")
        self.output.info("Appending PATH environment variable: {}".format(bindir))
        self.env_info.PATH.append(bindir)

        if self.settings.os == "Windows":
            self.cpp_info.system_libs.append("ws2_32")
            self.cpp_info.system_libs.append("wsock32")
            self.cpp_info.system_libs.append("shlwapi")

        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs.append("m")
            self.cpp_info.system_libs.append("dl")
            self.cpp_info.system_libs.append("rt")
            self.cpp_info.system_libs.append("pthread")
