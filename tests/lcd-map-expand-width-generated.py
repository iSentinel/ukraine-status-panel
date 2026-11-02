#!/usr/bin/env python3
"""Verify localized Home map-expand labels fit the generated LCD font."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LABELS=("ПОВНА","ПОЛНАЯ","FULL")

def widget(source: str) -> tuple[int,int]:
    start=source.index('id: dashboard_map_expand_button\n')
    block='\n'.join(source[start:].splitlines()[:20])
    width=int(re.search(r'\bwidth: (\d+)',block)[1])
    size=int(re.search(r'text_font: usp_i18n_(\d+)',block)[1])
    return size,width

def glyphs(main: str,size: int) -> dict[int,list[int]]:
    match=re.search(r'new\(usp_i18n_'+str(size)+r'\) font::Font\((\w+),',main)
    assert match, f'generated font usp_i18n_{size} missing'
    marker='static const font::Glyph '+match[1]+'[]'
    line=next(line for line in main.splitlines() if marker in line)
    pattern=r'\{(\d+), \([^)]*\), (\d+), (-?\d+), (-?\d+), (\d+), (\d+)\}'
    values={int(row[0]):[int(v) for v in row[1:]] for row in re.findall(pattern,line)}
    assert values
    return values

def text_width(text: str,values: dict[int,list[int]]) -> int:
    advance=right=0
    for char in text:
        assert ord(char) in values, f'missing glyph {char!r}'
        g=values[ord(char)]
        right=max(right,advance+g[1]+g[3])
        advance+=g[0]
    return max(right,advance)

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--main-cpp',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    assert not args.output.exists(), 'evidence already exists'
    raw=args.main_cpp.read_bytes(); main=raw.decode()
    source=(ROOT/'firmware/ukraine-status-panel.yaml').read_text()
    size,limit=widget(source); values=glyphs(main,size)
    samples=[]
    for language,text in enumerate(LABELS):
        width=text_width(text,values)
        assert width<=limit,(language,text,width,limit)
        samples.append({'language':language,'text':text,'width':width,'limit':limit,'font_size':size})
    result={'status':'PASS','measurements':3,'generated_main_sha256':hashlib.sha256(raw).hexdigest(),'samples':samples}
    args.output.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    print(f'LCD_MAP_EXPAND_WIDTH=PASS measurements=3 limit={limit} widths='+','.join(str(x['width']) for x in samples))
if __name__=='__main__': main()
