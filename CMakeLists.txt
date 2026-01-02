zephyr_library()

# Core Framework
zephyr_library_sources(src/core.c)

# V2 Nodes (Sequential processing framework)
zephyr_library_sources(src/nodes/node_sine_v2.c)
zephyr_library_sources(src/nodes/node_volume_v2.c)
zephyr_library_sources(src/nodes/node_spectrum_analyzer_v2.c)

# V1 Nodes (Legacy - only those without v2 versions)
zephyr_library_sources(src/nodes/node_splitter.c)
zephyr_library_sources(src/nodes/node_log_sink.c)
zephyr_library_sources(src/nodes/node_analyzer.c)

zephyr_include_directories(include)
