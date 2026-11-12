#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real-DT source-prefix profiles and qualified compiler controls.

No profile selector is read from the environment. The canonical integration
entry runs this complete selection on both supported cores. --profile is an
explicit standalone diagnostic only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

OWNER = Path(__file__).resolve().parents[2]
PROFILES = ('high-auto', 'low-auto', 'low-2', 'high-256', 'reject-default',
            'reject-boundary', 'reject-one', 'reject-child', 'reject-disabled-child', 'reject-second-node',
            'allow-disabled-listener', 'allow-disabled-device')
REJECTION = 'vector acceleration source limit excludes a routed listener'
SELECTORS = ('ZMK_TEST_SOURCE_BOUND_PROFILE', 'ZMK_VECTOR_SOURCE_PROFILE')


def fixture(profile):
    low = profile.startswith('low-') or profile == 'reject-one'
    limit = 0 if profile.endswith('auto') else 256 if profile == 'high-256' else 2
    if profile == 'reject-boundary':
        limit = 3
    elif profile == 'reject-one':
        limit = 1
    route = '<&sb_accel_b>' if profile == 'reject-second-node' else '<&sb_accel_a>'
    child = profile in ('reject-child', 'reject-disabled-child')
    status = 'status = "disabled";' if profile == 'allow-disabled-listener' else ''
    child_status = 'status = "disabled";' if profile == 'reject-disabled-child' else ''
    routes = (f'child {{ {child_status} layers = <0>; input-processors = {route}; }};'
              if child else f'input-processors = {route};')
    runtime = profile in ('high-auto', 'low-auto', 'low-2', 'high-256')
    routes_b = 'input-processors = <&sb_accel_b>;' if runtime or profile == 'reject-one' else ''
    listeners = f'''/ {{
    sb_listener_a: sb_listener_a {{ compatible = "zmk,input-listener";
        device = <&mmv>; {status} {routes} }};
    sb_listener_b: sb_listener_b {{ compatible = "zmk,input-listener";
        device = <&msc>; {status} {routes_b} }};
}};
'''
    includes = '''#include <behaviors.dtsi>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/kscan_mock.h>
'''
    text = listeners + includes if low else includes + listeners
    text += '''/ {
    keymap { compatible = "zmk,keymap"; base { bindings = <&kp A &kp B &kp C &kp D>; }; };
    sb_accel_a: sb_accel_a { compatible = "zmk,input-processor-vector-acceleration";
        #input-processor-cells = <0>; min-factor = <500>; max-factor = <3200>;
        unity-speed = <1200>; max-speed = <6000>; track-remainders; };
    sb_accel_b: sb_accel_b { compatible = "zmk,input-processor-vector-acceleration";
        #input-processor-cells = <0>; min-factor = <500>; max-factor = <3200>;
        unity-speed = <1200>; max-speed = <6000>; track-remainders; };
};
&kscan { events = <ZMK_MOCK_PRESS(0,0,30000)>; };
'''
    if profile == 'allow-disabled-device':
        text += '&mmv { status = "disabled"; };\n'
    conf = ('CONFIG_GPIO=n\nCONFIG_ZMK_BLE=n\nCONFIG_ZMK_POINTING=y\nCONFIG_ASSERT=y\n'
            'CONFIG_LOG_MODE_IMMEDIATE=y\n'
            f'CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_MAX_SOURCES={limit}\n')
    if runtime:
        conf += ('CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_SOURCE_BOUND=y\n'
                 f'CONFIG_ZMK_INPUT_VECTOR_ACCELERATION_TEST_SOURCE_BASE={0 if low else 3}\n')
    return text, conf, runtime, limit


def compile_command(build, source, output, sources=None):
    rows = json.loads((build / 'compile_commands.json').read_text())
    row = next(r for r in rows if r['file'] == str(OWNER / 'src/input_processor_vector_accel.c'))
    argv = shlex.split(row['command'])
    argv[argv.index('-o') + 1] = str(output)
    argv[argv.index('-c') + 1] = str(OWNER / 'tests/source-bound/capacity_probe.c')
    argv += [f'-DVECTOR_ACCEL_TEST_DRIVER_SOURCE="{source}"',
             f'-DVECTOR_ACCEL_TEST_SOURCES={sources or 2}', '-Werror=type-limits', '-fstack-usage']
    return argv, row['directory']


def require_rejection(result, diagnostic, name):
    if result.returncode == 0:
        raise RuntimeError(f'missing {name} rejection')
    if diagnostic not in result.stdout:
        raise RuntimeError(f'unclassified {name} compiler failure')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--core', required=True, choices=('stock', 'dya'))
    parser.add_argument('--profile', choices=PROFILES)
    args = parser.parse_args()
    if any(name in os.environ for name in SELECTORS):
        parser.error('ambient source-bound selectors are forbidden; use standalone --profile')
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        parser.error('source-bound output already exists')
    output = output.resolve()
    workspace = args.workspace.resolve()
    helper = OWNER / 'tests/integration/workspace-env.sh'
    selected = subprocess.check_output(['bash', '-c',
        'source "$1"; zmk_test_select_workspace "$2" || exit; env -0', '_', str(helper), str(workspace)])
    env = dict(os.environ)
    env.update(item.decode().split('=', 1) for item in selected.split(b'\0') if b'=' in item)
    roots = [OWNER.resolve(), workspace, *(Path(p).resolve() for p in env['ZEPHYR_MODULES'].split(';'))]
    if any(output == path or path in output.parents for path in roots):
        parser.error('source-bound output must be outside sources and dependencies')
    output.mkdir(parents=True, exist_ok=False)
    results = []
    (output / 'selection.json').write_text(json.dumps(dict(core=args.core, workspace=str(workspace),
        profiles=[args.profile] if args.profile else list(PROFILES),
        ZEPHYR_MODULES=env['ZEPHYR_MODULES'], Zephyr_DIR=env['Zephyr_DIR']), indent=2) + '\n')

    def recorded(argv, cwd, name):
        result = subprocess.run(argv, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + '.log')).write_text(result.stdout)
        results.append(dict(name=name, argv=argv, cwd=str(cwd), exit=result.returncode))
        (output / 'commands.json').write_text(json.dumps(results, indent=2) + '\n')
        return result

    for profile in ([args.profile] if args.profile else PROFILES):
        config = output / profile / 'config'
        config.mkdir(parents=True)
        keymap, conf, runtime, limit = fixture(profile)
        (config / 'native_sim.keymap').write_text(keymap)
        (config / 'native_sim.conf').write_text(conf)
        build = output / profile / 'build'
        command = ['west', 'build', '-s', 'zmk/app', '-d', str(build), '-b', 'native_sim//zmk_test_mock',
                   '--', '-DZephyr_DIR=' + env['Zephyr_DIR'], '-DZMK_CONFIG=' + str(config),
                   '-DZMK_EXTRA_MODULES=' + str(OWNER) + ';' + str(OWNER / 'tests/integration/module')]
        result = recorded(command, workspace, profile)
        if profile.startswith('reject-'):
            require_rejection(result, REJECTION, 'routed-listener')
            print('PASS:', args.core, profile, 'named rejection', flush=True)
            source = (OWNER / 'src/input_processor_vector_accel.c').read_text()
            if profile in ('reject-default', 'reject-boundary', 'reject-disabled-child', 'reject-second-node'):
                if profile == 'reject-default':
                    fragment = 'VECTOR_ACCEL_CHECK_ROUTE(DT_INST(index, zmk_input_listener), index)'
                    assert source.count(fragment) == 1
                    changed = source.replace(fragment, '')
                elif profile == 'reject-boundary':
                    fragment = '(index) < VECTOR_ACCEL_STREAM_COUNT'
                    assert source.count(fragment) == 1
                    changed = source.replace(fragment, '(index) <= VECTOR_ACCEL_STREAM_COUNT')
                elif profile == 'reject-disabled-child':
                    changed = source.replace('DT_FOREACH_CHILD_VARGS(', 'DT_FOREACH_CHILD_STATUS_OKAY_VARGS(')
                    assert changed != source
                else:
                    fragment = 'DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node, prop, idx), DT_DRV_COMPAT)'
                    assert source.count(fragment) == 1
                    changed = source.replace(fragment,
                        'DT_SAME_NODE(DT_PHANDLE_BY_IDX(node, prop, idx), DT_NODELABEL(sb_accel_a))')
                control_source = output / profile / 'control.c'
                control_source.write_text(changed)
                argv, cwd = compile_command(build, control_source, output / profile / 'control.o', limit)
                control = recorded(argv, cwd, profile + '-control')
                try:
                    require_rejection(control, REJECTION, 'routed-listener')
                except RuntimeError as error:
                    if str(error) != 'missing routed-listener rejection':
                        raise
                    print('EXPECTED FAIL control:', args.core, profile, str(error), flush=True)
                else:
                    raise RuntimeError('guard mutant did not hit missing routed-listener rejection')
            continue
        if result.returncode:
            raise RuntimeError(f'{profile} build failed; inspect retained log')
        if not runtime:
            print('PASS:', args.core, profile, 'unreachable route accepted', flush=True)
            continue
        expected = 5 if limit == 0 else limit
        argv, cwd = compile_command(build, OWNER / 'src/input_processor_vector_accel.c',
                                    output / profile / 'capacity.o', expected)
        result = recorded(argv, cwd, profile + '-capacity')
        if result.returncode:
            raise RuntimeError('compiled source capacity assertion failed')
        binary = build / 'zephyr/zmk.exe'
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        for repeat in range(1, 4):
            run = output / profile / ('run-' + str(repeat))
            run.mkdir()
            result = recorded(['timeout', '30', str(binary)], run, profile + '-run-' + str(repeat))
            marker = f'vector source-bound runtime: base={0 if profile.startswith("low-") else 3} max={limit}'
            if result.returncode or marker not in result.stdout or 'two sources, NULL, curve PASS' not in result.stdout:
                raise RuntimeError(f'{profile} runtime missing required completion')
            if limit == 256 and 'vector full prefix: 256 active source IDs PASS' not in result.stdout:
                raise RuntimeError('256-source runtime missing nonempty completion')
            assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
        print('PASS:', args.core, profile, 'three identical-binary runs, capacity', expected, flush=True)
    selected_profiles = [args.profile] if args.profile else list(PROFILES)
    (output / 'summary.json').write_text(json.dumps(dict(core=args.core, profiles=selected_profiles,
        profile_count=len(selected_profiles), commands=len(results),
        runtime_completions=sum('-run-' in row['name'] for row in results),
        qualified_guard_controls=sum(row['name'].endswith('-control') for row in results)), indent=2) + '\n')
    print('vector source-bound complete:', args.core, len(selected_profiles),
          'profiles PASS', flush=True)


if __name__ == '__main__':
    main()
