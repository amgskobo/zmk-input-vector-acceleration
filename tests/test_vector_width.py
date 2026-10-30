# SPDX-License-Identifier: MIT
"""Production vector arithmetic must not depend on software wide division."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class VectorWidth(unittest.TestCase):
    def test_32bit_object_has_no_wide_arithmetic_helpers(self):
        owner = Path(os.environ.get('ZMK_VECTOR_TEST_ROOT',
                                    str(Path(__file__).resolve().parents[1])))
        with tempfile.TemporaryDirectory(prefix='vector-width-') as folder:
            obj = Path(folder) / 'core.o'
            subprocess.run(['cc', '-m32', '-O2', '-std=c11', '-I', str(owner / 'include'),
                            '-c', str(owner / 'src/vector_accel_core.c'), '-o', str(obj)],
                           check=True, capture_output=True, text=True)
            symbols = subprocess.check_output(['nm', '--undefined-only', str(obj)], text=True)
            for helper in ('__divdi3', '__moddi3', '__divmoddi4', '__udivdi3',
                           '__umoddi3', '__udivmoddi4', '__muldi3'):
                self.assertNotIn(helper, symbols, symbols)


if __name__ == '__main__':
    unittest.main()
