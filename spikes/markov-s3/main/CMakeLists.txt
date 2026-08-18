idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
                       REQUIRES esp_timer
                       EMBED_TXTFILES corpus/calido.txt corpus/jugueton.txt
                                      corpus/curioso.txt corpus/urgente.txt
                                      corpus/seco.txt corpus/sonoliento.txt
                                      corpus/llano.txt)
# El bucle interior es una búsqueda binaria por palabra; -Os sería el trade malo.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
