from conan import ConanFile
from conan.tools.files import copy, get
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
import os


class WliteConan(ConanFile):
    name = "libwlite"
    version = "0.2.0"
    license = "MIT"
    url = "https://github.com/dbwarden-org/libwlite"
    homepage = "https://github.com/dbwarden-org/libwlite"
    description = "A small C library for SQLite schema management, migrations, and queries"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def requirements(self):
        self.requires("sqlite3/3.45.0")

    def export_sources(self):
        copy(self, "CMakeLists.txt", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "Makefile", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "LICENSE", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "include/*", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "wlite/*", src=self.recipe_folder, dst=self.export_sources_folder)
        copy(self, "cmake/*", src=self.recipe_folder, dst=self.export_sources_folder)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.recipe_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.libs = ["wlite"]
        self.cpp_info.includedirs = ["include"]
