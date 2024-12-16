from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import apply_conandata_patches, copy, export_conandata_patches, get, rmdir
from conan.tools.microsoft import is_msvc
from conan.tools.scm import Version
from conan.errors import ConanInvalidConfiguration
import os

required_conan_version = ">=1.55.0"


class SociConan(ConanFile):
    name = "soci"
    homepage = "https://github.com/SOCI/soci"
    url = "https://github.com/conan-io/conan-center-index"
    description = "The C++ Database Access Library "
    topics = ("mysql", "odbc", "postgresql", "sqlite3")
    license = "BSL-1.0"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared":           [True, False],
        "fPIC":             [True, False],
        "empty":            [True, False],
        "with_sqlite3":     [True, False],
        "with_db2":         [True, False],
        "with_odbc":        [True, False],
        "with_oracle":      [True, False],
        "with_firebird":    [True, False],
        "with_mysql":       [True, False],
        "with_postgresql":  [True, False],
        "with_boost":       [True, False],
    }
    default_options = {
        "shared":           False,
        "fPIC":             True,
        "empty":            False,
        "with_sqlite3":     False,
        "with_db2":         False,
        "with_odbc":        False,
        "with_oracle":      False,
        "with_firebird":    False,
        "with_mysql":       False,
        "with_postgresql":  False,
        "with_boost":       False,
    }

    def export_sources(self):
        export_conandata_patches(self)

    def layout(self):
        cmake_layout(self, src_folder="src")

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def requirements(self):
        if self.options.with_sqlite3:
            self.requires("sqlite3/3.41.1")
        if self.options.with_odbc and self.settings.os != "Windows":
            self.requires("odbc/2.3.11")
        if self.options.with_mysql:
            self.requires("libmysqlclient/8.0.31")
        if self.options.with_postgresql:
            self.requires("libpq/14.7")
        if self.options.with_boost:
            self.requires("boost/1.81.0")

    @property
    def _minimum_compilers_version(self):
        return {
            "Visual Studio": "14",
            "gcc": "4.8",
            "clang": "3.8",
            "apple-clang": "8.0"
        }

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, 11)

        compiler = str(self.settings.compiler)
        compiler_version = Version(self.settings.compiler.version.value)
        if compiler not in self._minimum_compilers_version:
            self.output.warning("{} recipe lacks information about the {} compiler support.".format(self.name, self.settings.compiler))
        elif compiler_version < self._minimum_compilers_version[compiler]:
            raise ConanInvalidConfiguration("{} requires a {} version >= {}".format(self.name, compiler, compiler_version))

        prefix  = "Dependencies for"
        message = "not configured in this conan package."
        if self.options.with_db2:
            # self.requires("db2/0.0.0") # TODO add support for db2
            raise ConanInvalidConfiguration("{} DB2 {} ".format(prefix, message))
        if self.options.with_oracle:
            # self.requires("oracle_db/0.0.0") # TODO add support for oracle
            raise ConanInvalidConfiguration("{} ORACLE {} ".format(prefix, message))
        if self.options.with_firebird:
            # self.requires("firebird/0.0.0") # TODO add support for firebird
            raise ConanInvalidConfiguration("{} firebird {} ".format(prefix, message))

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        tc = CMakeToolchain(self)

        tc.variables["SOCI_SHARED"] = self.options.shared
        tc.variables["SOCI_STATIC"] = not self.options.shared
        tc.variables["SOCI_TESTS"] = False
        tc.variables["SOCI_CXX11"] = True
        tc.variables["SOCI_EMPTY"] = self.options.empty
        tc.variables["WITH_SQLITE3"] = self.options.with_sqlite3
        tc.variables["WITH_DB2"] = self.options.with_db2
        tc.variables["WITH_ODBC"] = self.options.with_odbc
        tc.variables["WITH_ORACLE"] = self.options.with_oracle
        tc.variables["WITH_FIREBIRD"] = self.options.with_firebird
        tc.variables["WITH_MYSQL"] = self.options.with_mysql
        tc.variables["WITH_POSTGRESQL"] = self.options.with_postgresql
        tc.variables["WITH_BOOST"] = self.options.with_boost
        tc.generate()

        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        apply_conandata_patches(self)
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE_1_0.txt", dst=os.path.join(self.package_folder, "licenses"), src=self.source_folder)

        cmake = CMake(self)
        cmake.install()

        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "SOCI")

        target_suffix = "" if self.options.shared else "_static"
        lib_prefix = "lib" if is_msvc(self) and not self.options.shared else ""
        version = Version(self.version)
        lib_suffix = "_{}_{}".format(version.major, version.minor) if self.settings.os == "Windows" else ""

        # soci_core
        self.cpp_info.components["soci_core"].set_property("cmake_target_name", "SOCI::soci_core{}".format(target_suffix))
        self.cpp_info.components["soci_core"].libs = ["{}soci_core{}".format(lib_prefix, lib_suffix)]
        if self.options.with_boost:
            self.cpp_info.components["soci_core"].requires.append("boost::boost")

        # soci_empty
        if self.options.empty:
            self.cpp_info.components["soci_empty"].set_property("cmake_target_name", "SOCI::soci_empty{}".format(target_suffix))
            self.cpp_info.components["soci_empty"].libs = ["{}soci_empty{}".format(lib_prefix, lib_suffix)]
            self.cpp_info.components["soci_empty"].requires = ["soci_core"]

        # soci_sqlite3
        if self.options.with_sqlite3:
            self.cpp_info.components["soci_sqlite3"].set_property("cmake_target_name", "SOCI::soci_sqlite3{}".format(target_suffix))
            self.cpp_info.components["soci_sqlite3"].libs = ["{}soci_sqlite3{}".format(lib_prefix, lib_suffix)]
            self.cpp_info.components["soci_sqlite3"].requires = ["soci_core", "sqlite3::sqlite3"]

        # soci_odbc
        if self.options.with_odbc:
            self.cpp_info.components["soci_odbc"].set_property("cmake_target_name", "SOCI::soci_odbc{}".format(target_suffix))
            self.cpp_info.components["soci_odbc"].libs = ["{}soci_odbc{}".format(lib_prefix, lib_suffix)]
            self.cpp_info.components["soci_odbc"].requires = ["soci_core"]
            if self.settings.os == "Windows":
                self.cpp_info.components["soci_odbc"].system_libs.append("odbc32")
            else:
                self.cpp_info.components["soci_odbc"].requires.append("odbc::odbc")

        # soci_mysql
        if self.options.with_mysql:
            self.cpp_info.components["soci_mysql"].set_property("cmake_target_name", "SOCI::soci_mysql{}".format(target_suffix))
            self.cpp_info.components["soci_mysql"].libs = ["{}soci_mysql{}".format(lib_prefix, lib_suffix)]
            self.cpp_info.components["soci_mysql"].requires = ["soci_core", "libmysqlclient::libmysqlclient"]

        # soci_postgresql
        if self.options.with_postgresql:
            self.cpp_info.components["soci_postgresql"].set_property("cmake_target_name", "SOCI::soci_postgresql{}".format(target_suffix))
            self.cpp_info.components["soci_postgresql"].libs = ["{}soci_postgresql{}".format(lib_prefix, lib_suffix)]
            self.cpp_info.components["soci_postgresql"].requires = ["soci_core", "libpq::libpq"]

        # TODO: to remove in conan v2 once cmake_find_package* generators removed
        self.cpp_info.names["cmake_find_package"] = "SOCI"
        self.cpp_info.names["cmake_find_package_multi"] = "SOCI"
        self.cpp_info.components["soci_core"].names["cmake_find_package"] = "soci_core{}".format(target_suffix)
        self.cpp_info.components["soci_core"].names["cmake_find_package_multi"] = "soci_core{}".format(target_suffix)
        if self.options.empty:
            self.cpp_info.components["soci_empty"].names["cmake_find_package"] = "soci_empty{}".format(target_suffix)
            self.cpp_info.components["soci_empty"].names["cmake_find_package_multi"] = "soci_empty{}".format(target_suffix)
        if self.options.with_sqlite3:
            self.cpp_info.components["soci_sqlite3"].names["cmake_find_package"] = "soci_sqlite3{}".format(target_suffix)
            self.cpp_info.components["soci_sqlite3"].names["cmake_find_package_multi"] = "soci_sqlite3{}".format(target_suffix)
        if self.options.with_odbc:
            self.cpp_info.components["soci_odbc"].names["cmake_find_package"] = "soci_odbc{}".format(target_suffix)
            self.cpp_info.components["soci_odbc"].names["cmake_find_package_multi"] = "soci_odbc{}".format(target_suffix)
        if self.options.with_mysql:
            self.cpp_info.components["soci_mysql"].names["cmake_find_package"] = "soci_mysql{}".format(target_suffix)
            self.cpp_info.components["soci_mysql"].names["cmake_find_package_multi"] = "soci_mysql{}".format(target_suffix)
        if self.options.with_postgresql:
            self.cpp_info.components["soci_postgresql"].names["cmake_find_package"] = "soci_postgresql{}".format(target_suffix)
            self.cpp_info.components["soci_postgresql"].names["cmake_find_package_multi"] = "soci_postgresql{}".format(target_suffix)
