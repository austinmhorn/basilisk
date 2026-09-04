set(batch "${TEST_OUTPUT_DIR}/ai-sim-replay-batch.jsonl")
set(replay "${TEST_OUTPUT_DIR}/ai-sim-replay-single.jsonl")
set(batch_transitions "${TEST_OUTPUT_DIR}/ai-sim-replay-batch-transitions.jsonl")
set(replay_transitions "${TEST_OUTPUT_DIR}/ai-sim-replay-single-transitions.jsonl")

execute_process(
    COMMAND "${AI_SIM}" --matches 4 --seed 87123
        --output "${batch}" --transitions-output "${batch_transitions}"
    RESULT_VARIABLE batch_result OUTPUT_QUIET ERROR_VARIABLE batch_error)
if(NOT batch_result EQUAL 0)
    message(FATAL_ERROR "batch simulation failed: ${batch_error}")
endif()

execute_process(
    COMMAND "${AI_SIM}" --episode-index 2 --seed 87123
        --output "${replay}" --transitions-output "${replay_transitions}"
    RESULT_VARIABLE replay_result OUTPUT_QUIET ERROR_VARIABLE replay_error)
if(NOT replay_result EQUAL 0)
    message(FATAL_ERROR "episode replay failed: ${replay_error}")
endif()

file(STRINGS "${batch}" batch_episodes)
file(STRINGS "${replay}" replay_episodes)
list(LENGTH batch_episodes batch_count)
list(LENGTH replay_episodes replay_count)
if(NOT batch_count EQUAL 4 OR NOT replay_count EQUAL 1)
    message(FATAL_ERROR "unexpected episode output counts")
endif()
list(GET batch_episodes 2 batch_episode)
list(GET replay_episodes 0 replay_episode)
if(NOT batch_episode STREQUAL replay_episode)
    message(FATAL_ERROR "episode 2 replay differs from batch episode 2")
endif()

file(STRINGS "${batch_transitions}" batch_transition_lines)
file(STRINGS "${replay_transitions}" replay_transition_lines)
set(batch_episode_transitions "")
foreach(line IN LISTS batch_transition_lines)
    if(line MATCHES "\"episodeIndex\":2[,}]")
        list(APPEND batch_episode_transitions "${line}")
    endif()
endforeach()
if(NOT batch_episode_transitions STREQUAL replay_transition_lines)
    message(FATAL_ERROR "episode 2 transition replay differs from batch episode 2")
endif()

file(REMOVE "${batch}" "${replay}" "${batch_transitions}" "${replay_transitions}")
