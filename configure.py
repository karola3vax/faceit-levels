"""Pass local dependency paths and build options to AMBuild; it writes build files only."""

import sys

try:
  from ambuild2 import run
except ImportError:
  sys.stderr.write('AMBuild 2.2 or newer is required. Run scripts/build.py, which fetches it.\n')
  sys.exit(1)

parser = run.BuildParser(sourcePath=sys.path[0], api='2.2')
parser.options.add_argument('--hl2sdk-root', required=True)
parser.options.add_argument('--hl2sdk-manifests', required=True)
parser.options.add_argument('--mms-path', required=True)
parser.options.add_argument('--enable-debug', action='store_true', dest='debug')
parser.Configure()
