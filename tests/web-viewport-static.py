#!/usr/bin/env python3
"""Keep the mobile viewport contract and module-aware minification permanent."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'firmware/webui/usp-web-ui.src.js').read_text()
BUILD=(ROOT/'tools/web-ui/build.cjs').read_text()
RUNTIME=ROOT/'firmware/webui/usp-web-ui.js'
assert 'const viewport=node("meta")' in SRC
assert 'viewport.name="viewport"' in SRC
assert 'viewport.content="width=device-width"' in SRC
assert 'document.head.append(viewport)' in SRC
assert 'innerHTML' not in SRC and 'insertAdjacentHTML' not in SRC
assert 'module: true' in BUILD
print(f'WEB_VIEWPORT_STATIC=PASS runtime={RUNTIME.stat().st_size} module=1')
