cmake_minimum_required(VERSION 3.5)
if(NOT DEFINED ENV{ADF_PATH})
    set(ADF_PATH "$ENV{HOME}/esp/esp-adf")
    message(STATUS "ADF_PATH not set, defaulting to: ${ADF_PATH}")
else()
    set(ADF_PATH "$ENV{ADF_PATH}")
endif()

set(EXTRA_COMPONENT_DIRS "${ADF_PATH}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lyrat_record_play)

# Flash Tone Function
function(esptool_py_flash_customize_image target_name image_name offset image)
    idf_build_get_property(build_dir BUILD_DIR)
    file(RELATIVE_PATH image ${build_dir} ${image})
    set_property(TARGET ${target_name} APPEND PROPERTY FLASH_FILE "\"${offset}\" : \"${image}\"")
    set_property(TARGET ${target_name} APPEND PROPERTY FLASH_ENTRY "\"${image_name}\" : { \"offset\" : \"${offset}\", \"file\" : \"${image}\" }")
    set_property(TARGET ${target_name} APPEND PROPERTY IMAGES "${offset} ${image}")
endfunction()

# Flash the custom partition named `flash_tone`
set(partition flash_tone)
idf_build_get_property(project_dir PROJECT_DIR)
# Point to the parent wwe/tone/audio_tone.bin
set(image_file ${CMAKE_CURRENT_SOURCE_DIR}/../tone/audio_tone.bin)
partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
esptool_py_flash_customize_image(flash "${partition}" "${offset}" "${image_file}")
