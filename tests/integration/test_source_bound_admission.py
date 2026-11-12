# SPDX-License-Identifier: MIT
"""Canonical integration cannot silently select a smaller source matrix."""
import importlib.util
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SourceBoundAdmission(unittest.TestCase):
    def test_canonical_entry_rejects_ambient_and_argument_selectors(self):
        for arguments, variable in ((['low-2'], None), ([], 'ZMK_TEST_SOURCE_BOUND_PROFILE'),
                                    ([], 'ZMK_VECTOR_SOURCE_PROFILE')):
            with self.subTest(arguments=arguments, variable=variable):
                env = dict(os.environ)
                if variable:
                    env[variable] = 'low-2'
                result = subprocess.run(['bash', str(ROOT / 'tests/integration/run.sh'), *arguments],
                                        env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn('canonical integration forbids focused source-bound selectors',
                              result.stderr)

    def test_both_original_and_prefix_profiles_are_nonempty(self):
        spec = importlib.util.spec_from_file_location('vector_source_bound',
                                                     ROOT / 'tests/source-bound/run.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(len(module.PROFILES), 12)
        self.assertEqual(module.PROFILES[:4], ('high-auto', 'low-auto', 'low-2', 'high-256'))
        for profile, base, maximum in (('high-auto', 3, 0), ('low-auto', 0, 0),
                                       ('low-2', 0, 2), ('high-256', 3, 256)):
            with self.subTest(profile=profile):
                keymap, conf, runtime, limit = module.fixture(profile)
                self.assertTrue(runtime)
                self.assertEqual(limit, maximum)
                self.assertIn(f'TEST_SOURCE_BASE={base}', conf)
                self.assertEqual(keymap.count('input-processors = <&sb_accel_a>'), 1)
                self.assertEqual(keymap.count('input-processors = <&sb_accel_b>'), 1)
                self.assertIn('ZMK_MOCK_PRESS', keymap)


if __name__ == '__main__':
    unittest.main()
