"""Read a static JavaScript view with shared string constants expanded."""

from pathlib import Path
import re

DECLARATION = re.compile(r'\bconst\s+([A-Z][A-Z0-9_]*)\s*=\s*("(?:\\.|[^"\\])*")\s*;')
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\b[A-Z][A-Z0-9_]*\b')


def expand_string_constants(source):
    """Expand identifiers while preserving quoted text; never execute code."""
    constants = dict(DECLARATION.findall(source))
    return TOKEN.sub(lambda match: constants.get(match[0], match[0]), source)


def read_source(path):
    """Return the static view used by allowlist checks."""
    return expand_string_constants(Path(path).read_text(encoding="utf-8"))
