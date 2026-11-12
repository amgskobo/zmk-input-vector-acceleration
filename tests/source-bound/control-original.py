#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Require an original driver to fail the actual two-slot capacity assertion.

Use the retained unchanged 9e065431 driver and a completed low-2 build. An
unrelated compiler failure never qualifies this storage control.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--original-driver', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
spec = importlib.util.spec_from_file_location('source_bound', Path(__file__).with_name('run.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
argv, cwd = module.compile_command(args.build, args.original_driver, args.output / 'original.o', 2)
result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(args.output / 'compiler.log').write_text(result.stdout)
(args.output / 'command.json').write_text(json.dumps(dict(argv=argv, cwd=cwd, exit=result.returncode),
                                                    indent=2) + '\n')
module.require_rejection(result, 'vector source capacity allocation mismatch', 'original capacity')
print('EXPECTED FAIL control: original driver hits named source capacity allocation mismatch')
