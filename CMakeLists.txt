cmake_minimum_required(VERSION 3.16)

project(CamelionPlusPlus
        VERSION 1.0.0
        DESCRIPTION "Camelion++ - a Notepad++-style source code editor for Linux"
        LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

option(CAMELION_BUILD_PLUGINS "Build the bundled Camelion++ plugins" ON)
set(CAMELION_PLUGINLIST_PUBKEY "${CMAKE_SOURCE_DIR}/packaging/keys/pluginlist-ed25519-pub.pem"
    CACHE FILEPATH "Ed25519 public key (PEM) used to verify nppPluginList.so signatures")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(GMODULE REQUIRED IMPORTED_TARGET gmodule-2.0)
find_package(CURL REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
pkg_check_modules(TINYXML2 REQUIRED IMPORTED_TARGET tinyxml2)

# ---------------------------------------------------------------- Scintilla (GTK backend)
file(GLOB SCI_SRC  third_party/scintilla/src/*.cxx)
set(SCI_GTK_SRC
    third_party/scintilla/gtk/PlatGTK.cxx
    third_party/scintilla/gtk/ScintillaGTK.cxx
    third_party/scintilla/gtk/ScintillaGTKAccessible.cxx
    third_party/scintilla/gtk/scintilla-marshal.c)
add_library(scintilla STATIC ${SCI_SRC} ${SCI_GTK_SRC})
target_include_directories(scintilla PUBLIC third_party/scintilla/include
                                     PRIVATE third_party/scintilla/src)
target_compile_definitions(scintilla PUBLIC GTK PRIVATE NDEBUG)
target_link_libraries(scintilla PUBLIC PkgConfig::GTK3 PkgConfig::GMODULE)
target_compile_options(scintilla PRIVATE -w)

# ---------------------------------------------------------------- Lexilla
file(GLOB LEX_LIB  third_party/lexilla/lexlib/*.cxx)
file(GLOB LEX_LEXERS third_party/lexilla/lexers/*.cxx)
add_library(lexilla STATIC third_party/lexilla/src/Lexilla.cxx ${LEX_LIB} ${LEX_LEXERS})
target_include_directories(lexilla PUBLIC third_party/lexilla/include
                                   PRIVATE third_party/lexilla/lexlib third_party/scintilla/include)
target_compile_options(lexilla PRIVATE -w)

# ---------------------------------------------------------------- Versions of the engines
file(READ third_party/scintilla/version.txt SCI_VER)
string(STRIP "${SCI_VER}" SCI_VER)
string(REGEX REPLACE "^([0-9])([0-9])([0-9])$" "\\1.\\2.\\3" SCINTILLA_VERSION_TEXT "${SCI_VER}")
file(READ third_party/lexilla/version.txt LEX_VER)
string(STRIP "${LEX_VER}" LEX_VER)
string(REGEX REPLACE "^([0-9])([0-9])([0-9])$" "\\1.\\2.\\3" LEXILLA_VERSION_TEXT "${LEX_VER}")

# ---------------------------------------------------------------- Embedded public key
file(READ "${CAMELION_PLUGINLIST_PUBKEY}" CAMELION_PUBKEY_PEM)
configure_file(src/PluginListKey.h.in "${CMAKE_BINARY_DIR}/generated/PluginListKey.h" @ONLY)
configure_file(src/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

# ---------------------------------------------------------------- Camelion++
set(APP_SRC
    src/main.cpp
    src/Paths.cpp
    src/Encoding.cpp
    src/LangDefs.cpp
    src/Parameters.cpp
    src/Settings.cpp
    src/Editor.cpp
    src/MainWindow.cpp
    src/MainWindowCommands.cpp
    src/FindReplaceDlg.cpp
    src/TextOps.cpp
    src/Dialogs.cpp
    src/AutoComplete.cpp
    src/FunctionList.cpp
    src/PluginManager.cpp
    src/PluginList.cpp
    src/ElfReader.cpp
    src/PluginsAdmin.cpp
)
add_executable(camelion++ ${APP_SRC})
target_include_directories(camelion++ PRIVATE src sdk/include "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(camelion++ PRIVATE scintilla lexilla
    PkgConfig::GTK3 PkgConfig::GMODULE PkgConfig::TINYXML2
    CURL::libcurl LibArchive::LibArchive OpenSSL::Crypto nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS} pthread)
target_compile_definitions(camelion++ PRIVATE
    CAMELION_INSTALL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/camelion++"
    CAMELION_INSTALL_LIBDIR="${CMAKE_INSTALL_FULL_LIBDIR}/camelion++"
    CAMELION_RELATIVE_LIBDIR="${CMAKE_INSTALL_LIBDIR}/camelion++"
    CAMELION_SOURCE_DATADIR="${CMAKE_SOURCE_DIR}/data")
# Plugins resolve symbols only through the NppData function table, but export
# the executable's dynamic symbols anyway so plugins written against GTK share our copy.
set_target_properties(camelion++ PROPERTIES ENABLE_EXPORTS ON)
target_compile_options(camelion++ PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# ---------------------------------------------------------------- Bundled plugins
if(CAMELION_BUILD_PLUGINS)
  add_subdirectory(plugins)
endif()

# ---------------------------------------------------------------- Install
install(TARGETS camelion++ RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/camelion++)
install(FILES packaging/camelion++.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/camelion++.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/camelion++.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(DIRECTORY sdk/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/camelion++)
install(FILES docs/PLUGIN_SDK.md README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/nppPluginList/nppPluginList.so")
  install(FILES packaging/nppPluginList/nppPluginList.so
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/camelion++/plugins/Config)
endif()

# ---------------------------------------------------------------- Packaging (.deb / .tar.gz)
set(CPACK_PACKAGE_NAME "camelion-plus-plus")
set(CPACK_PACKAGE_VENDOR "Camelion++ project")
set(CPACK_PACKAGE_CONTACT "Camelion++ maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)
