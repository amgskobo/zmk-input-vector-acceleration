zephyr_library()

# ZMK's input-processor API is an application header, not a Zephyr header.
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)

zephyr_library_sources_ifdef(
  CONFIG_ZMK_INPUT_PROCESSOR_VECTOR_ACCELERATION
  src/input_processor_vector_accel.c
  src/vector_accel_core.c
)

zephyr_include_directories(include)

if(CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_CUSTOM_SETTINGS)
  # Goes on the app target rather than this module's library: it includes
  # zmk/studio/custom.h, which pulls ZMK's nanopb-generated proto/zmk/custom.pb.h,
  # and that header is only on the app target's include path.
  target_sources(app PRIVATE src/input_processor_vector_accel_custom_settings.c)
endif()
