# Bundled Camelion++ plugins.  Each one produces  <Name>/<Name>.so  under the
# build tree and is installed into  <prefix>/lib/camelion++/plugins/<Name>/.
function(camelion_plugin NAME)
  cmake_parse_arguments(P "GTK" "" "SOURCES" ${ARGN})
  add_library(${NAME} MODULE ${P_SOURCES})
  set_target_properties(${NAME} PROPERTIES
      PREFIX ""
      OUTPUT_NAME ${NAME}
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${NAME}"
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN ON)
  target_include_directories(${NAME} PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
  target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  # GTK symbols are provided by the host process; link anyway so that the
  # dependency is explicit for packaging tools.
  if(P_GTK)
    target_link_libraries(${NAME} PRIVATE PkgConfig::GTK3)
  endif()
  install(TARGETS ${NAME} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/camelion++/plugins/${NAME})
endfunction()

camelion_plugin(CamConverter SOURCES CamConverter/CamConverter.cpp)
camelion_plugin(CamMimeTools SOURCES CamMimeTools/CamMimeTools.cpp)
camelion_plugin(CamDocList GTK SOURCES CamDocList/CamDocList.cpp)
