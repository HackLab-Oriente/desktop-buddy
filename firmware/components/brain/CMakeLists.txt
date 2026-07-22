idf_component_register(
  SRCS "brain_cloud.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_http_client esp-tls mbedtls
)
