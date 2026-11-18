set(AISTHRA_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "main.c"
        "${AISTHRA_REPOSITORY_ROOT}/src/aisthra.c"
        "${AISTHRA_REPOSITORY_ROOT}/tests/embedded/public_contract.c"
        "${AISTHRA_REPOSITORY_ROOT}/tests/embedded/public_benchmark.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}"
        "${AISTHRA_REPOSITORY_ROOT}/include"
        "${AISTHRA_REPOSITORY_ROOT}/tests/embedded"
    REQUIRES
        esp_timer
        esp_system
        freertos
        heap
        pthread
)

# The Portage ESP-IDF integration does not provide the project's private
# platform-selection macro automatically. Keep the selection local to this
# component so native and other embedded consumers remain unchanged.
target_compile_definitions(${COMPONENT_LIB} PRIVATE AISTHRA_ESP_IDF=1)
