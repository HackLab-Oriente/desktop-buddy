# No EMBED_TXTFILES: the corpus belongs to the pack, not to the app binary.
# Baking sentences in would make editing one a reflash, which is the opposite
# of "behaviour is data" -- and it is what put a stray NUL in the vocabulary
# the first time round.
idf_component_register(SRCS "markov.c" INCLUDE_DIRS "include")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
