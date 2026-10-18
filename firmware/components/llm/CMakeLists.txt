# On-device inference engine — shared verbatim with the PC sim via ../../../common/llm
# (and the dish's minds, common/dish_ai.c: it calls the engine, and tankcore cannot require this component)
set(LLM ${CMAKE_CURRENT_LIST_DIR}/../../../common/llm)
idf_component_register(
    SRCS "${LLM}/q4_model.c" "${LLM}/word_tok.c" "${LLM}/advisor_core.c" "${LLM}/../dish_ai.c"
    INCLUDE_DIRS "${LLM}" "${LLM}/.."
    REQUIRES esp_timer tankcore
)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math)
if(CONFIG_POCKET_TANK_NO_PIE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE POCKET_TANK_NO_PIE)
endif()
