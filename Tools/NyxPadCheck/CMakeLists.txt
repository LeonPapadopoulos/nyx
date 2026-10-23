# NyxPadCheck.exe: lists gamepads, prints what they report and tries rumble, trigger effects and
# light on button presses; for checking pads by hand
add_executable(NyxPadCheck
    main.cpp
)

target_link_libraries(NyxPadCheck
    PRIVATE NyxEngine
)

# Includes engine headers, which include their generated reflection code
add_dependencies(NyxPadCheck NyxGeneratedReflection)
