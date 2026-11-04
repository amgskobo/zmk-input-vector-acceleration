# SPDX-License-Identifier: MIT
"""Compile the actual registration macro and check its scalar contract.

The typed registry implementation is covered by the DYA integration fixture.
This small check exercises the owner's declared defaults/bounds and DTS guards.
VECTOR_ACCEL_REVIEW_SETTINGS_REV=PRIOR_COMMIT is a before-repair negative control.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CustomSpeedContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vector-custom-speed-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        revision = os.environ.get("VECTOR_ACCEL_REVIEW_SETTINGS_REV")
        relative = "src/input_processor_vector_accel_custom_settings.c"
        source = subprocess.check_output(["git", "show", revision + ":" + relative], cwd=ROOT, text=True) if revision else (ROOT / relative).read_text()
        start = source.index("#define VECTOR_ACCEL_SETTINGS(n)")
        self.macro = source[start:source.index("\n\n", start)]

    def compile(self, max_speed):
        unit = self.work / "settings.c"
        unit.write_text(r"""
#include <assert.h>
#include <stdint.h>
#include <zmk-input-vector-acceleration/vector_accel_core.h>
#define BUILD_ASSERT(cond, text) _Static_assert(cond, text)
#define ZMK_INPUT_VECTOR_ACCELERATION_ASSERT_NAME_FITS(n, key)
#define test_min_factor 500
#define test_max_factor 3200
#define test_unity_speed 1200
#define DT_INST_PROP(n, field) test_##field
struct descriptor { int32_t value, min, max; };
#define VECTOR_ACCEL_SETTING(n, field, key, lo, hi) \
    static const struct descriptor field = {(int32_t)DT_INST_PROP(n, field), lo, hi};
""" + "\n#define test_max_speed " + str(max_speed) + "U\n" + self.macro + r"""
VECTOR_ACCEL_SETTINGS(0)
int main(void) {
    const struct descriptor *all[] = {&min_factor, &max_factor, &unity_speed, &max_speed};
    for (unsigned i=0; i<sizeof(all)/sizeof(all[0]); i++) {
        assert(all[i]->value >= all[i]->min && all[i]->value <= all[i]->max);
    }
    assert(unity_speed.max == INT32_MAX && max_speed.max == INT32_MAX);
    return 0;
}
""")
        binary = self.work / "settings"
        result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-I" + str(ROOT / "include"), str(unit), "-o", str(binary)], text=True, capture_output=True)
        return result, binary

    def test_default_above_one_million_fits_own_constraint(self):
        result, binary = self.compile(1000001)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_largest_int32_default_is_supported(self):
        result, binary = self.compile(2147483647)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unrepresentable_default_fails_named_build_guard(self):
        result, _ = self.compile(2147483648)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("max-speed exceeds the Custom Settings int32 range", result.stderr)


if __name__ == "__main__":
    unittest.main()
