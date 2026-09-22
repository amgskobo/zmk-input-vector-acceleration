# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_RUNTIME)
    target_sources(app PRIVATE src/runtime_test.c)
endif()

if(CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_PERSISTENCE)
    target_sources(app PRIVATE src/persistence_test.c)
endif()

if(CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_STUDIO)
    target_sources(app PRIVATE src/studio_test.c)
endif()
