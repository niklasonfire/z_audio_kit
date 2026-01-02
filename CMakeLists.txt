zephyr_library()

# Core Framework
zephyr_library_sources(src/core.c)

# Standard Nodes (kann man später auch via Kconfig einzeln schalten)
zephyr_library_sources(src/nodes/node_sine.c)
zephyr_library_sources(src/nodes/node_volume.c)
zephyr_library_sources(src/nodes/node_splitter.c)
zephyr_library_sources(src/nodes/node_log_sink.c)
zephyr_library_sources(src/nodes/node_analyzer.c)

# V2 Nodes (Sequential processing framework)
zephyr_library_sources(src/nodes/node_sine_v2.c)
zephyr_library_sources(src/nodes/node_volume_v2.c)
zephyr_library_sources(src/nodes/node_spectrum_analyzer_v2.c)

zephyr_include_directories(include)
