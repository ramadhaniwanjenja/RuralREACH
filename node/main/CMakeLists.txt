idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lora_driver packet aes_crypto audio_codec driver nvs_flash
)
