# nppPluginList for Camelion++ (Linux)
#
# Builds nppPluginList.so: the Linux counterpart of nppPluginList.dll.
#   * section .npp_pluginlist      : the plugin list JSON (src/pl.linux-<arch>.json)
#   * section .npp_pluginlist_sig  : 64-byte Ed25519 signature of that JSON
# Camelion++ reads both sections as data (it never dlopen()s this library) and
# refuses the list unless the signature verifies against its built-in public key.
#
#   cmake -S linux -B build-linux -DPLUGINLIST_ARCH=x64 -DPLUGINLIST_SIGNING_KEY=/path/to/ed25519-private.pem
#   cmake --build build-linux
cmake_minimum_required(VERSION 3.16)
project(nppPluginList VERSION 2.0 LANGUAGES C ASM)

if(NOT PLUGINLIST_ARCH)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(PLUGINLIST_ARCH arm64)
  else()
    set(PLUGINLIST_ARCH x64)
  endif()
endif()
set(PLUGINLIST_SIGNING_KEY "" CACHE FILEPATH "Ed25519 private key (PEM) used to sign the list; empty = unsigned build")

set(PL_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../src/pl.linux-${PLUGINLIST_ARCH}.json")
if(NOT EXISTS "${PL_JSON}")
  message(FATAL_ERROR "No plugin list for arch '${PLUGINLIST_ARCH}': ${PL_JSON}")
endif()
set(PL_SIG "${CMAKE_CURRENT_BINARY_DIR}/pl.linux-${PLUGINLIST_ARCH}.sig")

find_program(OPENSSL openssl)
if(PLUGINLIST_SIGNING_KEY)
  if(NOT OPENSSL)
    message(FATAL_ERROR "openssl is required to sign the plugin list")
  endif()
  add_custom_command(OUTPUT "${PL_SIG}"
    COMMAND "${OPENSSL}" pkeyutl -sign -rawin -inkey "${PLUGINLIST_SIGNING_KEY}" -in "${PL_JSON}" -out "${PL_SIG}"
    DEPENDS "${PL_JSON}" "${PLUGINLIST_SIGNING_KEY}"
    COMMENT "Signing pl.linux-${PLUGINLIST_ARCH}.json (Ed25519)"
    VERBATIM)
  set(SIGNED 1)
else()
  message(WARNING "PLUGINLIST_SIGNING_KEY is not set: building an UNSIGNED nppPluginList.so. "
                  "Camelion++ will refuse it outside of test mode.")
  file(WRITE "${PL_SIG}" "")
  set(SIGNED 0)
endif()

configure_file(pluginlist.S.in "${CMAKE_CURRENT_BINARY_DIR}/pluginlist.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pluginlist.S" PROPERTIES OBJECT_DEPENDS "${PL_JSON};${PL_SIG}")

add_library(nppPluginList SHARED nppPluginList.c "${CMAKE_CURRENT_BINARY_DIR}/pluginlist.S")
set_target_properties(nppPluginList PROPERTIES PREFIX "" OUTPUT_NAME nppPluginList
    C_VISIBILITY_PRESET hidden LINK_FLAGS "-Wl,-z,noexecstack")
target_compile_definitions(nppPluginList PRIVATE
    NPPPLUGINLIST_VERSION="${PROJECT_VERSION}" NPPPLUGINLIST_ARCH="linux-${PLUGINLIST_ARCH}" NPPPLUGINLIST_SIGNED=${SIGNED})

install(TARGETS nppPluginList LIBRARY DESTINATION lib/camelion++/plugins/Config)
