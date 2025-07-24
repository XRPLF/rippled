from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.apple import is_apple_os
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, rename, get, apply_conandata_patches, export_conandata_patches, replace_in_file, rmdir, rm, save
from conan.tools.microsoft import check_min_vs, msvc_runtime_flag, is_msvc, is_msvc_static_runtime
from conan.tools.scm import Version

import os

required_conan_version = ">=1.53"


class ProtobufConan(ConanFile):
    name = "protobuf"
    description = "Protocol Buffers - Google's data interchange format"
    topics = ("protocol-buffers", "protocol-compiler", "serialization", "rpc", "protocol-compiler")
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/protocolbuffers/protobuf"
    license = "BSD-3-Clause"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_zlib": [True, False],
        "with_rtti": [True, False],
        "lite": [True, False],
        "upb": [True, False],
        "debug_suffix": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_zlib": True,
        "with_rtti": True,
        "lite": False,
        "upb": False,
        "debug_suffix": True,
    }

    short_paths = True

    @property
    def _is_clang_cl(self):
        return self.settings.compiler == "clang" and self.settings.os == "Windows"

    @property
    def _is_clang_x86(self):
        return self.settings.compiler == "clang" and self.settings.arch == "x86"

    @property
    def _protobuf_release(self):
        current_ver = Version(self.version)
        return Version(f"{current_ver.minor}.{current_ver.patch}")

    def export_sources(self):
        export_conandata_patches(self)
        copy(self, "protobuf-conan-protoc-target.cmake", self.recipe_folder, os.path.join(self.export_sources_folder, "src"))

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

        if self._protobuf_release < "27.0":
            self.options.rm_safe("upb")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        if self.options.with_zlib:
            self.requires("zlib/[>=1.2.11 <2]")

        if self._protobuf_release >= "22.0":
            self.requires("abseil/[>=20230802.1 <=20250127.0]", transitive_headers=True)

    @property
    def _compilers_minimum_version(self):
        return {
            "gcc": "6",
            "clang": "5",
            "apple-clang": "10",
            "Visual Studio": "15",
            "msvc": "191",
        }

    def validate(self):
        if self.options.shared and is_msvc_static_runtime(self):
            raise ConanInvalidConfiguration("Protobuf can't be built with shared + MT(d) runtimes")

        if is_msvc(self) and self._protobuf_release >= "22" and self.options.shared and \
            not self.dependencies["abseil"].options.shared:
            raise ConanInvalidConfiguration("When building protobuf as a shared library on Windows, "
                                            "abseil needs to be a shared library too")
        if self._protobuf_release >= "30.1":
            check_min_cppstd(self, 17)
        elif self._protobuf_release >= "22.0":
            if self.settings.compiler.get_safe("cppstd"):
                check_min_cppstd(self, 14)
            else:
                minimum_version = self._compilers_minimum_version.get(str(self.settings.compiler), None)
                compiler_version = Version(self.settings.compiler.version)
                if minimum_version and compiler_version < minimum_version:
                    raise ConanInvalidConfiguration(
                        f"{self.ref} requires C++14, which your compiler does not support.",
                    )

        check_min_vs(self, "190")

        if self.settings.compiler == "clang":
            if Version(self.settings.compiler.version) < "4":
                raise ConanInvalidConfiguration(f"{self.ref} doesn't support clang < 4")

        if "abseil" in self.dependencies.host:
            abseil_cppstd = self.dependencies.host['abseil'].info.settings.compiler.cppstd
            if abseil_cppstd != self.settings.compiler.cppstd:
                raise ConanInvalidConfiguration(f"Protobuf and abseil must be built with the same compiler.cppstd setting")

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def build_requirements(self):
        if self._protobuf_release >= "30.1":
            self.tool_requires("cmake/[>=3.16 <4]")

    @property
    def _cmake_install_base_path(self):
        return os.path.join("lib", "cmake", "protobuf")

    def generate(self):
        tc = CMakeToolchain(self)
        if self._protobuf_release >= "30.1":
            tc.cache_variables["protobuf_LOCAL_DEPENDENCIES_ONLY"] = True
        tc.cache_variables["CMAKE_INSTALL_CMAKEDIR"] = self._cmake_install_base_path.replace("\\", "/")
        tc.cache_variables["protobuf_WITH_ZLIB"] = self.options.with_zlib
        tc.cache_variables["protobuf_BUILD_TESTS"] = False
        tc.cache_variables["protobuf_BUILD_PROTOC_BINARIES"] = self.settings.os != "tvOS"
        if not self.options.debug_suffix:
            tc.cache_variables["protobuf_DEBUG_POSTFIX"] = ""
        tc.cache_variables["protobuf_BUILD_LIBPROTOC"] = self.settings.os != "tvOS"
        tc.cache_variables["protobuf_DISABLE_RTTI"] = not self.options.with_rtti
        tc.cache_variables["protobuf_BUILD_LIBUPB"] = self.options.get_safe("upb")
        if self._protobuf_release >= "22.0":
            tc.cache_variables["protobuf_ABSL_PROVIDER"] = "package"
            if not self.settings.compiler.get_safe("cppstd") and self._protobuf_release >= "22.0":
                tc.variables["CMAKE_CXX_STANDARD"] = 14
        if is_msvc(self) or self._is_clang_cl:
            runtime = self.settings.get_safe("compiler.runtime")
            if runtime:
                tc.cache_variables["protobuf_MSVC_STATIC_RUNTIME"] = runtime == "static"
        if is_apple_os(self) and self.options.shared:
            # Workaround against SIP on macOS for consumers while invoking protoc when protobuf lib is shared
            tc.variables["CMAKE_INSTALL_RPATH"] = "@loader_path/../lib"

        if self.settings.os == "Linux":
            # Use RPATH instead of RUNPATH to help with specific case
            # in the grpc recipe when grpc_cpp_plugin is run with protoc
            # in the same build. RPATH ensures that the rpath in the binary
            # is respected for transitive dependencies too
            project_include = os.path.join(self.generators_folder, "protobuf_project_include.cmake")
            save(self, project_include, "add_link_options(-Wl,--disable-new-dtags)")
            tc.variables["CMAKE_PROJECT_INCLUDE"] = project_include
            # Note: conan2 only could be:
            # tc.extra_exelinkflags.append("-Wl,--disable-new-dtags")
            # tc.extra_sharedlinkflags.append("-Wl,--disable-new-dtags")

        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def _patch_sources(self):
        apply_conandata_patches(self)

        if self._protobuf_release < "22.0":
            # In older versions of protobuf, this file defines the `protobuf_generate` function
            protobuf_config_cmake = os.path.join(self.source_folder, "cmake", "protobuf-config.cmake.in")
            replace_in_file(self, protobuf_config_cmake, "@_protobuf_FIND_ZLIB@", "")
            replace_in_file(self, protobuf_config_cmake,
                "include(\"${CMAKE_CURRENT_LIST_DIR}/protobuf-targets.cmake\")",
                ""
            )

        # Disable a potential warning in protobuf-module.cmake.in
        # TODO: remove this patch? Is it really useful?
        protobuf_module_cmake = os.path.join(self.source_folder, "cmake", "protobuf-module.cmake.in")
        replace_in_file(self,
            protobuf_module_cmake,
            "if(DEFINED Protobuf_SRC_ROOT_FOLDER)",
            "if(0)\nif(DEFINED Protobuf_SRC_ROOT_FOLDER)",
        )
        replace_in_file(self,
            protobuf_module_cmake,
            "# Define upper case versions of output variables",
            "endif()",
        )

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake_root = "cmake" if Version(self.version) < "3.21" else None
        cmake.configure(build_script_folder=cmake_root)
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake", "utf8_range"))
        if self._protobuf_release < "22.0":
            rename(self, os.path.join(self.package_folder, self._cmake_install_base_path, "protobuf-config.cmake"),
                      os.path.join(self.package_folder, self._cmake_install_base_path, "protobuf-generate.cmake"))

        cmake_config_folder = os.path.join(self.package_folder, self._cmake_install_base_path)
        rm(self, "protobuf-config*.cmake", folder=cmake_config_folder)
        rm(self, "protobuf-targets*.cmake", folder=cmake_config_folder)
        copy(self, "protobuf-conan-protoc-target.cmake", src=self.source_folder, dst=cmake_config_folder)

        if not self.options.lite:
            rm(self, "libprotobuf-lite*", os.path.join(self.package_folder, "lib"))
            rm(self, "libprotobuf-lite*", os.path.join(self.package_folder, "bin"))

    def package_info(self):
        self.cpp_info.set_property("cmake_find_mode", "both")
        self.cpp_info.set_property("cmake_module_file_name", "Protobuf")
        self.cpp_info.set_property("cmake_file_name", "protobuf")
        self.cpp_info.set_property("pkg_config_name", "protobuf_full_package") # unofficial, but required to avoid side effects (libprotobuf component "steals" the default global pkg_config name)

        build_modules = [
            os.path.join(self._cmake_install_base_path, "protobuf-generate.cmake"),
            os.path.join(self._cmake_install_base_path, "protobuf-module.cmake"),
            os.path.join(self._cmake_install_base_path, "protobuf-options.cmake"),
            os.path.join(self._cmake_install_base_path, "protobuf-conan-protoc-target.cmake"),
        ]
        self.cpp_info.set_property("cmake_build_modules", build_modules)

        lib_prefix = "lib" if (is_msvc(self) or self._is_clang_cl) else ""
        lib_suffix = "d" if self.settings.build_type == "Debug" and self.options.debug_suffix else ""

        if self._protobuf_release >= "22.0":
            absl_deps = [f"abseil::{c}" for c in self.conan_data["absl_deps"][self.version]]

        if self._protobuf_release >= "22.0" and (not self.options.shared or self.options.get_safe("upb")):
            # utf8 libraries
            # it's a private dependency and unconditionally built as a static library, should only
            # be exposed when protobuf itself is static (or if upb is being built)
            self.cpp_info.components["utf8_range"].set_property("cmake_target_name", "utf8_range::utf8_range")
            self.cpp_info.components["utf8_validity"].set_property("cmake_target_name", "utf8_range::utf8_validity")
            # https://github.com/protocolbuffers/protobuf/blob/0d815c5b74281f081c1ee4b431a4d5bbb1615c97/third_party/utf8_range/CMakeLists.txt#L24
            if self._protobuf_release >= "30.1" and self.settings.os == "Windows":
                self.cpp_info.components["utf8_range"].libs = ["libutf8_range"]
                self.cpp_info.components["utf8_validity"].libs = ["libutf8_validity"]
            else:
                self.cpp_info.components["utf8_range"].libs = ["utf8_range"]
                self.cpp_info.components["utf8_validity"].libs = ["utf8_validity"]
            self.cpp_info.components["utf8_validity"].requires = ["abseil::absl_strings"]

        if self.options.get_safe("upb"):
            # upb libraries: note that these are unconditionally static
            self.cpp_info.components["upb"].set_property("cmake_target_name", "protobuf::libupb")
            self.cpp_info.components["upb"].libs = [lib_prefix + "upb" + lib_suffix]
            self.cpp_info.components["upb"].requires = ["utf8_range"]

        # libprotobuf
        self.cpp_info.components["libprotobuf"].set_property("cmake_target_name", "protobuf::libprotobuf")
        self.cpp_info.components["libprotobuf"].set_property("pkg_config_name", "protobuf")
        self.cpp_info.components["libprotobuf"].builddirs.append(self._cmake_install_base_path)
        self.cpp_info.components["libprotobuf"].libs = [lib_prefix + "protobuf" + lib_suffix]
        if self.options.with_zlib:
            self.cpp_info.components["libprotobuf"].requires = ["zlib::zlib"]
        if self._protobuf_release >= "22.0":
            self.cpp_info.components["libprotobuf"].requires.extend(absl_deps)
            if not self.options.shared:
                self.cpp_info.components["libprotobuf"].requires.extend(["utf8_validity"])

        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.components["libprotobuf"].system_libs.extend(["m", "pthread"])
            if self._is_clang_x86 or "arm" in str(self.settings.arch):
                self.cpp_info.components["libprotobuf"].system_libs.append("atomic")
        if self.settings.os == "Android":
            self.cpp_info.components["libprotobuf"].system_libs.append("log")
        if self.settings.os == "Windows":
            if self.options.shared:
                self.cpp_info.components["libprotobuf"].defines = ["PROTOBUF_USE_DLLS"]

        # libprotoc
        if self.settings.os != "tvOS":
            self.cpp_info.components["libprotoc"].set_property("cmake_target_name", "protobuf::libprotoc")
            self.cpp_info.components["libprotoc"].libs = [lib_prefix + "protoc" + lib_suffix]
            self.cpp_info.components["libprotoc"].requires = ["libprotobuf"]
            if self._protobuf_release >= "22.0":
                self.cpp_info.components["libprotoc"].requires.extend(absl_deps)

        # libprotobuf-lite
        if self.options.lite:
            self.cpp_info.components["libprotobuf-lite"].set_property("cmake_target_name", "protobuf::libprotobuf-lite")
            self.cpp_info.components["libprotobuf-lite"].set_property("pkg_config_name", "protobuf-lite")
            self.cpp_info.components["libprotobuf-lite"].builddirs.append(self._cmake_install_base_path)
            self.cpp_info.components["libprotobuf-lite"].libs = [lib_prefix + "protobuf-lite" + lib_suffix]
            if self.settings.os in ["Linux", "FreeBSD"]:
                self.cpp_info.components["libprotobuf-lite"].system_libs.extend(["m", "pthread"])
                if self._is_clang_x86 or "arm" in str(self.settings.arch):
                    self.cpp_info.components["libprotobuf-lite"].system_libs.append("atomic")
            if self.settings.os == "Windows":
                if self.options.shared:
                    self.cpp_info.components["libprotobuf-lite"].defines = ["PROTOBUF_USE_DLLS"]
            if self.settings.os == "Android":
                self.cpp_info.components["libprotobuf-lite"].system_libs.append("log")
            if self._protobuf_release >= "22.0":
                self.cpp_info.components["libprotobuf-lite"].requires.extend(absl_deps)
                if not self.options.shared:
                    self.cpp_info.components["libprotobuf-lite"].requires.extend(["utf8_validity"])

        # TODO: to remove in conan v2 once cmake_find_package* & pkg_config generators removed
        self.cpp_info.filenames["cmake_find_package"] = "Protobuf"
        self.cpp_info.filenames["cmake_find_package_multi"] = "protobuf"
        self.cpp_info.names["pkg_config"] ="protobuf_full_package"
        for generator in ["cmake_find_package", "cmake_find_package_multi"]:
            self.cpp_info.components["libprotobuf"].build_modules[generator] = build_modules
        if self.options.lite:
            for generator in ["cmake_find_package", "cmake_find_package_multi"]:
                self.cpp_info.components["libprotobuf-lite"].build_modules[generator] = build_modules
        self.env_info.PATH.append(os.path.join(self.package_folder, "bin"))
