zephyr_library()

zephyr_library_sources_ifdef(
  CONFIG_ZMK_INPUT_PROCESSOR_VECTOR_ACCELERATION
  src/input_processor_vector_accel.c
  src/vector_accel_core.c
)

zephyr_include_directories(include)
