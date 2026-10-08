# Seed new builds without replacing a player's saved button bindings.
if(NOT EXISTS "${INPUT_TARGET}")
    configure_file("${INPUT_SOURCE}" "${INPUT_TARGET}" COPYONLY)
endif()
