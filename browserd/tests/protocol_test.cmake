execute_process(
  COMMAND "${BROWSERD}" --dry-run
  INPUT_FILE "${INPUT}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
  RESULT_VARIABLE result
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "browserd failed (${result}): ${error}")
endif()

foreach(expected
    "\"type\":\"terminal_metrics\""
    "\"type\":\"created\""
    "\"mode\":\"insert\""
    "https://example.org/日本語")
  string(FIND "${output}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "missing '${expected}' in output: ${output}")
  endif()
endforeach()

string(FIND "${output}" "\"type\":\"error\"" error_event)
if(NOT error_event EQUAL -1)
  message(FATAL_ERROR "unexpected protocol error: ${output}")
endif()

execute_process(
  COMMAND "${BROWSERD}" --version
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error
  RESULT_VARIABLE version_result
)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "protocol=3")
  message(FATAL_ERROR
    "browserd did not report protocol=3 (${version_result}): ${version_output}${version_error}")
endif()
