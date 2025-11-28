# Component (target) configuration

# Place WiFi helper code in flash (not IRAM) to save space
set_source_files_properties(wifi_helper.c PROPERTIES COMPILE_FLAGS "-fno-inline-functions  -fno-inline-small-functions")
