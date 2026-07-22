# Berry VM host. The VM lives in the git submodule ./berry.
# One-time codegen after cloning the submodule:
#   cd components/berry_host/berry && mkdir -p generate && \
#     python3 tools/coc/coc -o generate src default -c default/berry_conf.h
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
    INCLUDE_DIRS "include" "berry/src" "berry/default" "berry/generate"
    REQUIRES bus
  )
  target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_BERRY=1)
  # Upstream Berry C sources vs IDF's -Werror: relax what they trip over.
  target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter -fno-strict-aliasing -Wno-error)
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
