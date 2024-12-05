import os
import glob
import shutil

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, collect_libs, copy, export_conandata_patches, get, rm, rmdir
from conan.tools.microsoft import check_min_vs, is_msvc, is_msvc_static_runtime
from conan.tools.scm import Version

required_conan_version = ">=1.53.0"


class RocksDBConan(ConanFile):
    name = "rocksdb"
    homepage = "https://github.com/facebook/rocksdb"
    license = ("GPL-2.0-only", "Apache-2.0")
    url = "https://github.com/conan-io/conan-center-index"
    description = "A library that provides an embeddable, persistent key-value store for fast storage"
    topics = ("database", "leveldb", "facebook", "key-value")
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "lite": [True, False],
        "with_gflags": [True, False],
        "with_snappy": [True, False],
        "with_lz4": [True, False],
        "with_zlib": [True, False],
        "with_zstd": [True, False],
        "with_tbb": [True, False],
        "with_jemalloc": [True, False],
        "enable_sse": [False, "sse42", "avx2"],
        "use_rtti": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "lite": False,
        "with_snappy": False,
        "with_lz4": False,
        "with_zlib": False,
        "with_zstd": False,
        "with_gflags": False,
        "with_tbb": False,
        "with_jemalloc": False,
        "enable_sse": False,
        "use_rtti": False,
    }

    @property
    def _min_cppstd(self):
        return "11" if Version(self.version) < "8.8.1" else "17"

    @property
    def _compilers_minimum_version(self):
        return {} if self._min_cppstd == "11" else {
                "apple-clang": "10",
                "clang": "7",
                "gcc": "7",
                "msvc": "191",
                "Visual Studio": "15",
            }

    def export_sources(self):
        export_conandata_patches(self)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
        if self.settings.arch != "x86_64":
            del self.options.with_tbb
        if self.settings.build_type == "Debug":
            self.options.use_rtti = True  # Rtti are used in asserts for debug mode...

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        if self.options.with_gflags:
            self.requires("gflags/2.2.2")
        if self.options.with_snappy:
            self.requires("snappy/1.1.10")
        if self.options.with_lz4:
            self.requires("lz4/1.9.4")
        if self.options.with_zlib:
            self.requires("zlib/[>=1.2.11 <2]")
        if self.options.with_zstd:
            self.requires("zstd/1.5.5")
        if self.options.get_safe("with_tbb"):
            self.requires("onetbb/2021.10.0")
        if self.options.with_jemalloc:
            self.requires("jemalloc/5.3.0")

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, self._min_cppstd)

        minimum_version = self._compilers_minimum_version.get(str(self.settings.compiler), False)
        if minimum_version and Version(self.settings.compiler.version) < minimum_version:
            raise ConanInvalidConfiguration(
                f"{self.ref} requires C++{self._min_cppstd}, which your compiler does not support."
            )

        if self.settings.arch not in ["x86_64", "ppc64le", "ppc64", "mips64", "armv8"]:
            raise ConanInvalidConfiguration("Rocksdb requires 64 bits")

        check_min_vs(self, "191")

        if self.version == "6.20.3" and \
           self.settings.os == "Linux" and \
           self.settings.compiler == "gcc" and \
           Version(self.settings.compiler.version) < "5":
            raise ConanInvalidConfiguration("Rocksdb 6.20.3 is not compilable with gcc <5.") # See https://github.com/facebook/rocksdb/issues/3522

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["FAIL_ON_WARNINGS"] = False
        tc.variables["WITH_TESTS"] = False
        tc.variables["WITH_TOOLS"] = False
        tc.variables["WITH_CORE_TOOLS"] = False
        tc.variables["WITH_BENCHMARK_TOOLS"] = False
        tc.variables["WITH_FOLLY_DISTRIBUTED_MUTEX"] = False
        if is_msvc(self):
            tc.variables["WITH_MD_LIBRARY"] = not is_msvc_static_runtime(self)
        tc.variables["ROCKSDB_INSTALL_ON_WINDOWS"] = self.settings.os == "Windows"
        tc.variables["ROCKSDB_LITE"] = self.options.lite
        tc.variables["WITH_GFLAGS"] = self.options.with_gflags
        tc.variables["WITH_SNAPPY"] = self.options.with_snappy
        tc.variables["WITH_LZ4"] = self.options.with_lz4
        tc.variables["WITH_ZLIB"] = self.options.with_zlib
        tc.variables["WITH_ZSTD"] = self.options.with_zstd
        tc.variables["WITH_TBB"] = self.options.get_safe("with_tbb", False)
        tc.variables["WITH_JEMALLOC"] = self.options.with_jemalloc
        tc.variables["ROCKSDB_BUILD_SHARED"] = self.options.shared
        tc.variables["ROCKSDB_LIBRARY_EXPORTS"] = self.settings.os == "Windows" and self.options.shared
        tc.variables["ROCKSDB_DLL" ] = self.settings.os == "Windows" and self.options.shared
        tc.variables["USE_RTTI"] = self.options.use_rtti
        if not bool(self.options.enable_sse):
            tc.variables["PORTABLE"] = True
            tc.variables["FORCE_SSE42"] = False
        elif self.options.enable_sse == "sse42":
            tc.variables["PORTABLE"] = True
            tc.variables["FORCE_SSE42"] = True
        elif self.options.enable_sse == "avx2":
            tc.variables["PORTABLE"] = False
            tc.variables["FORCE_SSE42"] = False
        # not available yet in CCI
        tc.variables["WITH_NUMA"] = False
        tc.generate()

        deps = CMakeDeps(self)
        if self.options.with_jemalloc:
            deps.set_property("jemalloc", "cmake_file_name", "JeMalloc")
            deps.set_property("jemalloc", "cmake_target_name", "JeMalloc::JeMalloc")
        deps.generate()

    def build(self):
        apply_conandata_patches(self)
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def _remove_static_libraries(self):
        rm(self, "rocksdb.lib", os.path.join(self.package_folder, "lib"))
        for lib in glob.glob(os.path.join(self.package_folder, "lib", "*.a")):
            if not lib.endswith(".dll.a"):
                os.remove(lib)

    def _remove_cpp_headers(self):
        for path in glob.glob(os.path.join(self.package_folder, "include", "rocksdb", "*")):
            if path != os.path.join(self.package_folder, "include", "rocksdb", "c.h"):
                if os.path.isfile(path):
                    os.remove(path)
                else:
                    shutil.rmtree(path)

    def package(self):
        copy(self, "COPYING", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "LICENSE*", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        if self.options.shared:
            self._remove_static_libraries()
            self._remove_cpp_headers() # Force stable ABI for shared libraries
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))

    def package_info(self):
        cmake_target = "rocksdb-shared" if self.options.shared else "rocksdb"
        self.cpp_info.set_property("cmake_file_name", "RocksDB")
        self.cpp_info.set_property("cmake_target_name", f"RocksDB::{cmake_target}")
        # TODO: back to global scope in conan v2 once cmake_find_package* generators removed
        self.cpp_info.components["librocksdb"].libs = collect_libs(self)
        if self.settings.os == "Windows":
            self.cpp_info.components["librocksdb"].system_libs = ["shlwapi", "rpcrt4"]
            if self.options.shared:
                self.cpp_info.components["librocksdb"].defines = ["ROCKSDB_DLL"]
        elif self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.components["librocksdb"].system_libs = ["pthread", "m"]
        if self.options.lite:
            self.cpp_info.components["librocksdb"].defines.append("ROCKSDB_LITE")

        # TODO: to remove in conan v2 once cmake_find_package* generators removed
        self.cpp_info.names["cmake_find_package"] = "RocksDB"
        self.cpp_info.names["cmake_find_package_multi"] = "RocksDB"
        self.cpp_info.components["librocksdb"].names["cmake_find_package"] = cmake_target
        self.cpp_info.components["librocksdb"].names["cmake_find_package_multi"] = cmake_target
        self.cpp_info.components["librocksdb"].set_property("cmake_target_name", f"RocksDB::{cmake_target}")
        if self.options.with_gflags:
            self.cpp_info.components["librocksdb"].requires.append("gflags::gflags")
        if self.options.with_snappy:
            self.cpp_info.components["librocksdb"].requires.append("snappy::snappy")
        if self.options.with_lz4:
            self.cpp_info.components["librocksdb"].requires.append("lz4::lz4")
        if self.options.with_zlib:
            self.cpp_info.components["librocksdb"].requires.append("zlib::zlib")
        if self.options.with_zstd:
            self.cpp_info.components["librocksdb"].requires.append("zstd::zstd")
        if self.options.get_safe("with_tbb"):
            self.cpp_info.components["librocksdb"].requires.append("onetbb::onetbb")
        if self.options.with_jemalloc:
            self.cpp_info.components["librocksdb"].requires.append("jemalloc::jemalloc")
