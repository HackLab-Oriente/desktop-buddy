idf_component_register(SRCS "markov.c" INCLUDE_DIRS "include"
                       REQUIRES esp_timer
                       EMBED_TXTFILES corpus/calido.txt corpus/jugueton.txt
                                      corpus/curioso.txt corpus/urgente.txt
                                      corpus/seco.txt corpus/sonoliento.txt
                                      corpus/llano.txt)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
