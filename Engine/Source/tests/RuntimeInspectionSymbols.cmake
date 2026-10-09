# Inspect the whole runtime archive, including objects a small test might not link.
execute_process(
    COMMAND "${LINKER}" /dump /linkermember:1 "${RUNTIME_LIBRARY}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Could not inspect runtime library: ${error}")
endif()

if(symbols MATCHES "(RegisterLastItem|RegisterWindow|RegisterRegion|IsInspectingSources|SetInspectingSources|SetSourceItemCallback)@UI@Nyx"
    OR symbols MATCHES "ReflectionSourceRegistry|RegisterRuntimeReflectedSources")
    message(FATAL_ERROR "The runtime library contains UI inspection symbols.")
endif()

message(STATUS "Runtime library contains no Nyx UI inspection symbols.")
