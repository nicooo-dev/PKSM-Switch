#!/usr/bin/env python3
import hashlib, json, os, pathlib, urllib.request

CDN_BASE = os.environ.get('PKSM_CDN_BASE', 'https://cdn.sigkill.tech/')
DEST_ROOT = pathlib.Path(os.environ.get('PKSM_SWITCH_ASSETS', '/tmp/pksm-switch-assets'))
SPRITES_DIR = DEST_ROOT / 'sprites'
DATA_JSON = DEST_ROOT / 'data.json'

def sha256_file(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for c in iter(lambda:f.read(1024*1024), b''): h.update(c)
    return h.hexdigest()

def download(url,dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as r, open(dst,'wb') as f: f.write(r.read())

def main():
    DEST_ROOT.mkdir(parents=True, exist_ok=True)
    SPRITES_DIR.mkdir(parents=True, exist_ok=True)
    data_url = CDN_BASE.rstrip('/') + '/assets/data.json'
    print('Downloading', data_url)
    download(data_url, DATA_JSON)
    data=json.loads(DATA_JSON.read_text(encoding='utf-8'))
    pokemon=data.get('pokemon',[])
    downloaded=0; skipped=0
    for i,e in enumerate(pokemon,1):
        fp=e.get('file_path','')
        if not fp: continue
        name=fp.split('/')[-1]
        if not name: continue
        dst=SPRITES_DIR/name
        if dst.exists() and dst.stat().st_size>0:
            skipped+=1; continue
        url=CDN_BASE.rstrip('/') + '/assets/sprites/' + name
        try:
            download(url,dst); downloaded+=1
        except Exception as ex:
            print('WARN',name,ex)
        if i%250==0: print(f'Progress: {i}/{len(pokemon)}')
    manifest={
        'cdn':CDN_BASE,
        'data_json_sha256':sha256_file(DATA_JSON),
        'sprites_count':len(list(SPRITES_DIR.glob('*.png'))),
        'downloaded':downloaded,
        'skipped':skipped
    }
    (DEST_ROOT/'manifest.local.json').write_text(json.dumps(manifest,indent=2), encoding='utf-8')
    print(json.dumps(manifest,indent=2))
    print('Copy folder to SD: /switch/PKSM/assets')

if __name__=='__main__': main()