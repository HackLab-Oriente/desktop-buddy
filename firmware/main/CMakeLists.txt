idf_component_register(
  SRCS "main.cpp"
  REQUIRES bus senses expressions brain webui berry_host nvs_flash
)

# cJSON recurses ~96 bytes of stack per nesting level (measured against the
# vendored copy) and defaults to allowing 1000 -- about 96 KB. The bus task has
# 6144 and the brain task 10240, so a document 64 levels deep walks off the end
# of a stack long before cJSON refuses it, and the canary is only checked at a
# context switch. 20 is past anything this project's own formats need.
idf_build_set_property(COMPILE_OPTIONS "-DCJSON_NESTING_LIMIT=20" APPEND)
