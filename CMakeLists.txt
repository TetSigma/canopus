cmake_minimum_required(VERSION 3.16)
 
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sample_project)
 
# ── Workaround: GCC 15.x IRA pass segfault on esp_lcd_panel_rgb.c ──
#
# The xtensa-esp-elf-gcc 15.2.0 toolchain (shipped with ESP-IDF v6.0.x)
# has an internal compiler error in the IRA register-allocation pass when
# compiling esp_lcd_panel_rgb.c with -Og.  This is a known GCC bug:
#   https://github.com/espressif/esp-idf/issues/12180
#   https://github.com/espressif/esp-idf/issues/10568
#
# Fix: compile that one file at -O0 to skip the broken optimisation pass.
# This has no effect on your application code — only on the RGB LCD driver
# which you are not using (the CO5300 is QSPI, not RGB parallel).
#
# The hook fires after the esp_lcd component target is created.
idf_build_set_property(COMPILE_OPTIONS "" APPEND)
 
cmake_language(DEFER CALL
    set_source_files_properties
        "$ENV{IDF_PATH}/components/esp_lcd/rgb/esp_lcd_panel_rgb.c"
    PROPERTIES
        COMPILE_FLAGS "-O0"
)
 
