"""Packs the release zip: dist/Radiance-<version>.zip.

Everything goes into the game folder (the mod is client side only): the Greeneye Loader (dbgcore.dll),
greeneye/radiance/ (the release DLL from build.bat, mod.json and the ini with its defaults), the readme and
the license. Run build.bat first, then: python -I tools/package.py
"""
import json
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOADER = os.path.join(ROOT, '..', 'GreeneyeLoader', 'bin', 'dbgcore.dll')
MOD = 'greeneye/radiance/'

version = json.load(open(os.path.join(ROOT, 'mod.json')))['version']
dll = os.path.join(ROOT, 'bin', 'loader', 'radiance.dll')
code = open(dll, 'rb').read()
# the dev build's test aids (the effect survey, the controller log, the test cursor file)
for mark in (b'vfxdump', b'dev: pad nav', 'cursor'.encode('utf-16-le')):
    if mark in code:
        sys.exit('bin\\loader holds a dev build; run build.bat')
sources = [os.path.join(ROOT, 'src', f) for f in os.listdir(os.path.join(ROOT, 'src'))]
if os.path.getmtime(dll) < max(os.path.getmtime(f) for f in sources):
    sys.exit('bin\\loader\\radiance.dll is older than the source; run build.bat')
if version.encode() not in code:
    sys.exit('the DLL does not carry version %s; run build.bat' % version)
ini = open(os.path.join(ROOT, 'radiance.ini'), encoding='utf-8').read()
readme = open(os.path.join(ROOT, 'dist', 'README.txt'), encoding='utf-8').read()
if version not in ini.splitlines()[0] or version not in readme.splitlines()[0]:
    sys.exit('radiance.ini or dist/README.txt names another version than %s' % version)
defaults = {'Enabled': 'true', 'Color': '255, 137, 88', 'Intensity': '1.0', 'Radius': '45', 'Language': 'auto',
            'ToggleLight': 'F8, NoPad', 'OpenMenu': 'Shift+F8, Pad:LB+Left'}
for key, value in defaults.items():
    found = re.search(r'^%s=(.*)$' % key, ini, re.M)
    if not found or found.group(1).strip() != value:
        sys.exit('radiance.ini does not hold the default %s=%s' % (key, value))
files = [
    (LOADER, 'dbgcore.dll'),
    (dll, MOD + 'radiance.dll'),
    (os.path.join(ROOT, 'mod.json'), MOD + 'mod.json'),
    (os.path.join(ROOT, 'radiance.ini'), MOD + 'radiance.ini'),
    (os.path.join(ROOT, 'dist', 'README.txt'), 'README.txt'),
    (os.path.join(ROOT, 'LICENSE'), 'LICENSE'),
]
out = os.path.join(ROOT, 'dist', f'Radiance-{version}.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for src, arc in files:
        z.write(src, arc)
print(out, os.path.getsize(out), 'bytes,', len(files), 'files')
