idf_component_register(
  SRCS "webui.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_http_server esp_wifi nvs_flash cjson
  EMBED_TXTFILES "www/index.html" "www/style.css" "www/favicon.svg"
)

