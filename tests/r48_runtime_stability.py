#!/usr/bin/env python3
import os
import asyncio
from pathlib import Path
from aioesphomeapi import LogLevel
from history_native_api_common import build_client

ROOT = Path(__file__).resolve().parents[1]
CONFIG = Path(os.environ.get('USP_CONFIG', ROOT / 'firmware' / 'ukraine-status-panel.yaml'))
ADDRESS = os.environ.get('USP_ADDRESS', 'ukraine-status-panel.local')

def text_of(msg):
    v=getattr(msg,'message',b'')
    return v.decode('utf-8',errors='replace') if isinstance(v,bytes) else str(v)

async def main():
    c=build_client(CONFIG,ADDRESS)
    await c.connect(login=True)
    lines=[]
    def cb(msg):
        t=text_of(msg); lines.append(t); print(t)
    stop=c.subscribe_logs(cb,log_level=LogLevel.LOG_LEVEL_WARN,dump_config=False)
    try:
        info=await c.device_info(); print('VERSION='+str(info.project_version))
        await asyncio.sleep(65)
    finally:
        try: stop()
        except Exception: pass
        await c.disconnect()
    longops=[x for x in lines if 'took a long time' in x]
    fatal=[x for x in lines if any(s in x.lower() for s in ('guru meditation','task watchdog','watchdog timeout','panic abort','assert failed'))]
    ws=[x for x in lines if 'websocket' in x.lower() and ('error' in x.lower() or 'failed' in x.lower())]
    tls=[x for x in lines if 'tls' in x.lower() and ('failed' in x.lower() or 'verify' in x.lower())]
    print(f'WARN_LINES={len(lines)}')
    print(f'LONG_OPERATION_WARNINGS={len(longops)}')
    print(f'FATAL_EVENTS={len(fatal)}')
    print(f'WEBSOCKET_ERROR_EVENTS={len(ws)}')
    print(f'TLS_ERROR_EVENTS={len(tls)}')
    if fatal or ws or tls:
        raise RuntimeError('runtime stability fatal/transport error')
    print('R48_RUNTIME_STABILITY=PASS')

asyncio.run(main())
