file(GLOB CODEC2_SRCS "*.c")
idf_component_register(SRCS ${CODEC2_SRCS} INCLUDE_DIRS ".")
# Vendored third-party code (Codec2): don't apply the project's strict -Werror.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
