# Berry VM host. The VM lives in the git submodule ./berry.
#
# port/berry_conf.h is OURS and shadows the submodule's: it turns the desktop
# reference interpreter into a pack sandbox. It must come first on the include
# path, and the codegen must be run against it -- the constant tables are
# generated per configuration.
#
# One-time codegen after cloning the submodule:
#   cd components/berry_host/berry && mkdir -p generate && \
#     python3 tools/coc/coc -o generate src default -c ../port/berry_conf.h
# Without the submodule (or before codegen) this component compiles a stub
# that logs a warning, so the project always builds.
set(BERRY_DIR ${CMAKE_CURRENT_LIST_DIR}/berry)

if(EXISTS ${BERRY_DIR}/generate/be_const_strtab.h)
  file(GLOB BERRY_SRC ${BERRY_DIR}/src/*.c)
  # default/ is the reference port: module table + OS port. berry.c is the
  # standalone REPL's main() — excluded.
  list(APPEND BERRY_SRC ${BERRY_DIR}/default/be_modtab.c ${BERRY_DIR}/default/be_port.c)
  idf_component_register(
    SRCS "berry_host.cpp" ${BERRY_SRC}
    INCLUDE_DIRS "include" "port" "berry/src" "berry/default" "berry/generate"
    REQUIRES bus
    PRIV_REQUIRES esp_timer
  )
  target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_BERRY=1)
  # Only the upstream C sources get the relaxations. Applied to the component
  # they also covered berry_host.cpp, which quietly lost the house -Werror.
  set_source_files_properties(${BERRY_SRC} PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-parameter;-fno-strict-aliasing;-Wno-error")
else()
  if(EXISTS ${BERRY_DIR}/src)
    message(WARNING "berry submodule present but not generated — run the coc "
                    "codegen (see comment at top of ${CMAKE_CURRENT_LIST_FILE}); "
                    "building WITHOUT Berry for now")
  endif()
  idf_component_register(
    SRCS "berry_host.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bus
  )
endif()
