#!/usr/bin/env python3
"""web/index.html as one file, for publishing: every <script src> put
inline, and the firmware the page installs put in as EMBEDDED, so it
fetches nothing. Usage: site.py OUT"""
import json, re, sys
from pathlib import Path

WEB = Path(__file__).resolve().parent.parent / 'web'
FILES = ['firmware/digimacro-v2.json', 'firmware/digimacro-v2.hex',
         'firmware/digimacro-v2-core.hex']

def script(m):
    code = (WEB / m.group(1)).read_text()
    if '</script' in code.lower():
        sys.exit(f'{m.group(1)} has </script in it')
    return f'<script>\n{code}</script>'

page = (WEB / 'index.html').read_text()
page, n = re.subn(r'<script src="([^"]+)"></script>', script, page)
if not n or 'src=' in page:
    sys.exit('index.html: scripts not where they were expected')
embedded = {f: open(WEB / f, newline='').read() for f in FILES}  # as is, CRs too
first = page.index('<script>')
page = (page[:first]
        + '<script>\nconst EMBEDDED = '
        + json.dumps(embedded).replace('</', '<\\/') + ';\n</script>\n'
        + page[first:])
Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)
Path(sys.argv[1]).write_text(page)
print(f'{sys.argv[1]}: {len(page)} bytes, {n} scripts and {len(FILES)} files in it')
