# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT

import os
import subprocess

Import('env')
with open(env['PROJECT_TOOL_S']) as f:
    exec(f.read())

toolchain_sysroot = os.environ.get('CONFIG_TOOLCHAIN_SYSROOT', '')
toolchain_multiarch = env.get('GCC_DUMPMACHINE', 'aarch64-linux-gnu')

for config_header_name in ('global_config.h', 'lvgl_config.h'):
    config_header = os.path.join(
        env['PROJECT_PATH'], 'build', 'config', config_header_name
    )
    if os.path.exists(config_header):
        with open(config_header, encoding='utf-8') as config_file:
            config_content = config_file.read()
        config_content = config_content.replace('#define DEBUG 1\n', '')
        with open(config_header, 'w', encoding='utf-8') as config_file:
            config_file.write(config_content)

libv4l_dev_file = 'libv4l-dev_1.32.0-2ubuntu1_arm64.deb'
libv4l_dev_deb = check_wget_down(
    'https://ports.ubuntu.com/ubuntu-ports/pool/main/v/v4l-utils/' + libv4l_dev_file,
    libv4l_dev_file,
)
libv4l_dev_root = os.path.join(
    os.environ['GIT_REPO_PATH'], libv4l_dev_file[:-4]
)
if not os.path.exists(libv4l_dev_root):
    os.makedirs(libv4l_dev_root, exist_ok=True)
    subprocess.check_call(['dpkg-deb', '-x', libv4l_dev_deb, libv4l_dev_root])


SRCS = append_srcs_dir(ADir('src'))
SRCS = [src for src in SRCS if not str(src).endswith('src/utils/json_helper.cpp')]
if os.environ.get('CONFIG_CAMERA_APP_USE_DESKTOP') != '1':
    SRCS = [src for src in SRCS if not str(src).endswith('src/input/sdl_keypad.cpp')]

INCLUDE = [
    ADir('src'),
    os.path.join(os.environ['EXT_COMPONENTS_PATH'], 'cp0_lvgl', 'src'),
    os.path.join(os.environ['SDK_PATH'], 'components', 'utilities', 'party', 'fmt', 'include'),
    os.path.join(libv4l_dev_root, 'usr', 'include'),
]
PRIVATE_INCLUDE = []
REQUIREMENTS = [
    'lvgl_component',
    'Miniaudio',
    'pthread',
    'dl',
    'camera',
    'camera-base',
    ':libfmt.so.10',
    'freetype',
    'png16',
    'jpeg',
    'z',
    'm',
]
STATIC_LIB = [
    os.path.join(libv4l_dev_root, 'usr', 'lib', 'aarch64-linux-gnu', 'libv4l2.a'),
    os.path.join(libv4l_dev_root, 'usr', 'lib', 'aarch64-linux-gnu', 'libv4lconvert.a'),
]
DYNAMIC_LIB = []
DEFINITIONS = ['-DCAMERA_APP_SCONS_BUILD', '-std=c++17', '-O2']
DEFINITIONS_PRIVATE = []
LDFLAGS = ['-Wl,--allow-shlib-undefined']
LINK_SEARCH_PATH = []
STATIC_FILES = [(ADir('assets'), 'assets')]

with open(os.path.join(env['PROJECT_PATH'], 'build/config/global_config.h'),
    encoding='utf-8',) as f_in:
    with open(os.path.join(env['PROJECT_PATH'], 'build/config/camera_app_config.h'),
        'w',encoding='utf-8',) as f_out:
        f_out.write('#pragma once\n\n')
        for line in f_in.readlines():
            if line.startswith('#define CONFIG_CAMERA_APP_'):
                f_out.write(
                    line.replace('CONFIG_CAMERA_APP_USE_DESKTOP', 'USE_DESKTOP').replace('CONFIG_CAMERA_APP_', 'APP_')
                )


if toolchain_sysroot:
    INCLUDE += [
        os.path.join(toolchain_sysroot, 'usr', 'include'),
        os.path.join(toolchain_sysroot, 'usr', 'include', 'libcamera'),
        os.path.join(toolchain_sysroot, 'usr', 'include', 'freetype2'),
        os.path.join(toolchain_sysroot, 'usr', 'include', 'libpng16'),
        os.path.join(toolchain_sysroot, 'usr', 'include', toolchain_multiarch),
    ]
    toolchain_lib_path = os.path.join(
        toolchain_sysroot, 'usr', 'lib', toolchain_multiarch
    )
    LINK_SEARCH_PATH += [toolchain_lib_path]
    LDFLAGS += [
        f'-Wl,-rpath-link,{toolchain_lib_path}',
        f'-B{toolchain_lib_path}',
    ]

lvgl_component = list(
    filter(lambda component: component['target'] == 'lvgl_component', env['COMPONENTS'])
)[0]

if toolchain_sysroot:
    lvgl_component['INCLUDE'] += [
        os.path.join(toolchain_sysroot, 'usr', 'include', 'freetype2'),
        os.path.join(toolchain_sysroot, 'usr', 'include', 'libpng16'),
    ]

env['COMPONENTS'].append({
    'target': env['PROJECT_NAME'],
    'SRCS': SRCS,
    'INCLUDE': INCLUDE,
    'PRIVATE_INCLUDE': PRIVATE_INCLUDE,
    'REQUIREMENTS': REQUIREMENTS,
    'STATIC_LIB': STATIC_LIB,
    'DYNAMIC_LIB': DYNAMIC_LIB,
    'DEFINITIONS': DEFINITIONS,
    'DEFINITIONS_PRIVATE': DEFINITIONS_PRIVATE,
    'LDFLAGS': LDFLAGS,
    'LINK_SEARCH_PATH': LINK_SEARCH_PATH,
    'STATIC_FILES': STATIC_FILES,
    'REGISTER': 'project',
})
