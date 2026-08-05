idf_component_register(SRCS "main.cpp" INCLUDE_DIRS ".")
# Optimisation 1: the project builds -Os globally, which is right for the
# firmware as a whole but wrong for a per-pixel inner loop. Exempt this one.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
