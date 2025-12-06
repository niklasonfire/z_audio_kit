cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(memory_test)

# WICHTIG: Falls "z_audio_kit" keine echte Library ist (sondern nur Source-Files via Zephyr Module),
# verursacht "target_link_libraries" Fehler.
# Besser ist es meist, es wegzulassen, wenn CONFIG_AUDIO_FRAMEWORK gesetzt ist.
# target_link_libraries(app PRIVATE z_audio_kit) <--- Im Zweifel auskommentieren/löschen

target_sources(app PRIVATE src/main.c)