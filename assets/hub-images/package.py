from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import html, json, hashlib, zipfile

root=Path(__file__).parent
items=[]
for folder in ['native','rendered']:
    for file in sorted((root/folder).glob('*.png')):
        kind='Native Owner-build screenshot (cropped)' if folder=='native' else ('Native-layout illustration with fictional tracks' if 'illustration' in file.name else 'Original Hub HTML rendered with demo state')
        with Image.open(file) as im:
            im.verify()
        with Image.open(file) as im:
            width,height=im.size
        items.append(dict(path=file.relative_to(root).as_posix(),name=file.stem,kind=kind,width=width,height=height,sha256=hashlib.sha256(file.read_bytes()).hexdigest()))
(root/'manifest.json').write_text(json.dumps(items,indent=2),encoding='utf-8')
cards=[]
for item in items:
    cards.append(f'<article data-kind="{html.escape(item["kind"])}"><a href="{item["path"]}" target="_blank"><img loading="lazy" src="{item["path"]}" alt="{item["name"]}"></a><h2>{item["name"]}</h2><p>{item["kind"]}</p><small>{item["width"]} × {item["height"]} px</small><a class="download" href="{item["path"]}" download>Download PNG</a></article>')
gallery='''<!doctype html><html lang="en"><head><meta charset="utf-8"><title>RearSilver Hub image library</title><style>*{box-sizing:border-box}body{margin:0;background:#0b0f14;color:#e6e8eb;font:15px system-ui,sans-serif}header{padding:40px;max-width:1100px}h1{font-size:32px}p{line-height:1.6;color:#aebed1}main{padding:0 40px 40px;display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}article{border:1px solid #303b4a;border-radius:12px;overflow:hidden;background:#111821;padding:16px}img{width:100%;height:240px;object-fit:contain;background:#080d12}h2{font-size:16px;overflow-wrap:anywhere}small{color:#9db0c7}.download{display:block;margin-top:12px;color:#00d4ff}a{color:#00d4ff}</style></head><body><header><h1>RearSilver Hub image library</h1><p>31 images · 7 September 2026. Click a thumbnail for the full-resolution PNG.</p><p>Native captures come from the installed Owner build. HTML captures use the current source interface with labelled demo state. The two player images are layout illustrations, not screenshots. OBS captures are being supplied separately by Ben.</p><p>Native exports omit the saved music strip and window chrome. Connected-account capture shows the approved public names RearSilver and FrontSilver. Demo accounts are explicitly fictional. No credentials, commercial artwork, video thumbnails, or private file paths are included in these exports.</p><p><a href="README.md">Capture notes</a> · <a href="publication-candidates.zip">Download image ZIP</a></p></header><main>'''+''.join(cards)+'</main></body></html>'
(root/'index.html').write_text(gallery,encoding='utf-8')
# A compact review sheet; full-resolution exports remain untouched.
cellw,cellh,cols=360,250,4
sheet=Image.new('RGB',(cellw*cols,cellh*((len(items)+cols-1)//cols)), '#e7ebf0')
d=ImageDraw.Draw(sheet)
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',13)
for n,item in enumerate(items):
    x=(n%cols)*cellw;y=(n//cols)*cellh
    with Image.open(root/item['path']) as im:
        im=im.convert('RGB');im.thumbnail((344,208))
        sheet.paste(im,(x+(cellw-im.width)//2,y+4))
    d.text((x+8,y+215),item['name'],font=font,fill='#152438')
    d.text((x+8,y+233),'Native' if item['path'].startswith('native/') else 'Illustration' if 'illustration' in item['name'] else 'HTML demo',font=font,fill='#455568')
sheet.save(root/'contact-sheet.jpg',quality=90)
with zipfile.ZipFile(root/'publication-candidates.zip','w',zipfile.ZIP_DEFLATED) as z:
    for item in items:z.write(root/item['path'],item['path'])
    for name in ['README.md','manifest.json','index.html','contact-sheet.jpg']:z.write(root/name,name)
print(f'Packaged {len(items)} verified PNG files. Originals and preview source excluded from ZIP.')
