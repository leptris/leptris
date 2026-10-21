"""Conan 2 recipe for libleptris — builds the library from the release tarball.

Usage (from the repository root):

    conan create packaging/conan --build=missing

The recipe is CCI-shaped (cmake_layout + CMakeToolchain/CMakeDeps, source
fetched from the release tarball, default package ID) so it can be carried
into conan-center-index with minimal edits — but it lives in-repo (Gate C5);
submission to ConanCenter is a separate, owner-approved step, see
packaging/README.md.

Version bump: set `version` below to the released engine tag (vX.Y.Z, cut by
.github/workflows/release.yml). It must match the CMakeLists.txt project()
version of that tag.
"""

import os

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import get

required_conan_version = ">=2.0"


class LeptrisConan(ConanFile):
    name = "leptris"
    # Bump together with the engine tag vX.Y.Z (see module docstring).
    version = "1.9.379"
    description = "Fast XML parser and XPath evaluator in pure C"
    license = "MIT"
    homepage = "https://github.com/leptris/leptris"
    url = "https://github.com/leptris/leptris"
    topics = ("xml", "xpath", "parser", "sax", "c")
    package_type = "library"

    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        # Mirrors the CMake options LEPTRIS_ENABLE_UTF8PROC / _ICONV / _DUCET.
        "with_utf8proc": [True, False],
        "with_iconv": [True, False],
        "with_ducet": [True, False],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "with_utf8proc": True,
        "with_iconv": True,
        "with_ducet": True,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def requirements(self):
        if self.options.with_utf8proc:
            self.requires("utf8proc/[>=2.9 <4]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        # The project gates its library targets on its own options
        # (LEPTRIS_BUILD_STATIC/LEPTRIS_BUILD_SHARED in src/CMakeLists.txt),
        # not CMake's BUILD_SHARED_LIBS.
        tc.variables["LEPTRIS_BUILD_SHARED"] = bool(self.options.shared)
        tc.variables["LEPTRIS_BUILD_STATIC"] = not bool(self.options.shared)
        tc.variables["BUILD_TESTING"] = False
        tc.variables["BUILD_EXAMPLES"] = False
        # Library-only package: the CLI/man pages/benchmarks ship through the
        # Homebrew formula and the Debian packages instead.
        tc.variables["LEPTRIS_BUILD_CLI"] = False
        tc.variables["LEPTRIS_BUILD_BENCHMARKS"] = False
        tc.variables["LEPTRIS_BUILD_MAN_PAGES"] = False
        tc.variables["LEPTRIS_BUILD_DOCS"] = False
        tc.variables["LEPTRIS_ENABLE_UTF8PROC"] = bool(self.options.with_utf8proc)
        tc.variables["LEPTRIS_ENABLE_ICONV"] = bool(self.options.with_iconv)
        # DUCET collation table requires utf8proc (see CMakeLists.txt).
        tc.variables["LEPTRIS_ENABLE_DUCET"] = bool(
            self.options.with_ducet and self.options.with_utf8proc
        )
        tc.generate()

    def source(self):
        get(
            self,
            f"https://github.com/leptris/leptris/archive/refs/tags/v{self.version}.tar.gz",
            strip_root=True,
        )

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "leptris")
        # Matches the upstream CMake export: leptris::leptris (see
        # src/CMakeLists.txt ALIAS targets).
        self.cpp_info.set_property("cmake_target_name", "leptris::leptris")
        self.cpp_info.set_property("pkg_config_name", "leptris")
        self.cpp_info.libs = ["leptris"]
        if self.options.with_utf8proc:
            self.cpp_info.requires = ["utf8proc::utf8proc"]
        if self.options.with_iconv:
            # CMake's FindIconv links the Apple system iconv; on
            # glibc/Linux iconv lives in libc and needs no extra lib.
            if self.settings.os in ("Macos", "iOS", "watchOS", "tvOS", "visionOS"):
                self.cpp_info.system_libs = ["iconv"]
