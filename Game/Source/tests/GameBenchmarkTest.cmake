# Runs NyxGame's benchmark mode for a second on a scene, along a camera path, in a hidden window,
# and checks what it did: exit code 0, and a CSV with its header, a row a frame and the summary's
# three rows, every row with every column.
#
# cmake -DGAME=<NyxGame.exe> -DSCENE=<scene file> -DOUT_DIR=<folder> -P GameBenchmarkTest.cmake

cmake_minimum_required(VERSION 3.23)

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}")
set(csv "${OUT_DIR}/Benchmark.csv")
set(cameraPath "${OUT_DIR}/Camera.txt")
file(WRITE "${cameraPath}" "# seconds, position, look-at\n0  0 4 -8  0 0 0\n1  8 4 0  0 0 0\n")

execute_process(
    COMMAND "${GAME}" "${SCENE}" --window-hidden --benchmark 1 --benchmark-warmup 0.5
        --benchmark-out "${csv}" --benchmark-camera "${cameraPath}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    TIMEOUT 120)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The benchmark ended with '${result}', not 0:\n${output}")
endif()
if(NOT EXISTS "${csv}")
    message(FATAL_ERROR "The benchmark wrote no ${csv}:\n${output}")
endif()

file(STRINGS "${csv}" lines)
list(LENGTH lines lineCount)
if(lineCount LESS 14)
    message(FATAL_ERROR "The CSV has ${lineCount} lines; a second of frames should be at least ten, with a header and the summary")
endif()

list(GET lines 0 header)
if(NOT header MATCHES "^frame,time_s,frame_ms,.*,gpu_frame_ms,.*,draw_calls,.*,decals,point_lights,particle_emitters,particles,lightning_bolts$")
    message(FATAL_ERROR "The CSV's header is '${header}'")
endif()
string(REGEX MATCHALL "," headerCommas "${header}")
list(LENGTH headerCommas columns)

# The frames, numbered from 0, then "average", "1% low" and "worst"
math(EXPR lastFrameLine "${lineCount} - 4")
set(frame 0)
set(time "")
foreach(index RANGE 1 ${lastFrameLine})
    list(GET lines ${index} line)
    if(NOT line MATCHES "^${frame},([0-9.]+),[0-9.]+,")
        message(FATAL_ERROR "Line ${index} should be frame ${frame}: '${line}'")
    endif()
    set(time ${CMAKE_MATCH_1})
    math(EXPR frame "${frame} + 1")
endforeach()
if(time LESS 1.0)
    message(FATAL_ERROR "The frames end at ${time} s, before the second the benchmark should record")
endif()

set(index ${lastFrameLine})
foreach(row "average" "1% low" "worst")
    math(EXPR index "${index} + 1")
    list(GET lines ${index} line)
    if(NOT line MATCHES "^${row},[0-9.]+,[0-9.]+,")
        message(FATAL_ERROR "Line ${index} should be the summary's '${row}': '${line}'")
    endif()
endforeach()

foreach(line IN LISTS lines)
    string(REGEX MATCHALL "," commas "${line}")
    list(LENGTH commas lineColumns)
    if(NOT lineColumns EQUAL columns)
        message(FATAL_ERROR "A line has ${lineColumns} commas, the header ${columns}: '${line}'")
    endif()
endforeach()

message(STATUS "The benchmark recorded ${frame} frames in ${time} s")
