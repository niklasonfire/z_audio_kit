# deps/modules/z_audio_kit/samples/basic_sine/CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)

# 1. Zephyr Paket laden (MUSS vor project() stehen)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# 2. Projekt definieren
project(basic_sine)

# 3. Quellcode hinzufügen
target_sources(app PRIVATE src/main.c)

# 4. Library einbinden
# Überprüft, ob die Library geladen wurde (via Zephyr Module System)
if(TARGET z_audio_kit)
    target_link_libraries(app PRIVATE z_audio_kit)
else()
    message(WARNING "z_audio_kit Target not found! Is the module path correct?")
endif()