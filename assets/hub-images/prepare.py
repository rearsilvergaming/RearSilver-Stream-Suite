from pathlib import Path
import shutil
import re
import json
from PIL import Image

root = Path(__file__).parent
source = Path(r'C:\Users\Ben\Desktop\Streaming\Streamer Tools\obs-streamer-tools\obs-streamer-tools\RearSilver-Stream-Suite')
preview = root / 'preview'
preview.mkdir(exist_ok=True)
(root / 'rendered').mkdir(exist_ok=True)
(root / 'native').mkdir(exist_ok=True)
shutil.copy2(source / 'assets/fonts/Sora-Variable.ttf', preview / 'Sora-Variable.ttf')
pages = ['library', 'suite-settings', 'stream-tools', 'overlay-designer', 'feedback-diagnostics', 'commands']
catalogue = (source / 'src/music_player/command_catalogue.hpp').read_text(encoding='utf-8')
definitions=[]
for match in re.finditer(r'\{"([^"\n]+)", "([^"\n]+)", "([^"\n]+)", "([^"\n]+)", "([^"\n]+)", (RsRole\w+)\}', catalogue):
    ident, category, label, syntax, description, role = match.groups()
    definitions.append(dict(id=ident,category=category,label=label,syntax=syntax,description=description,defaults={role.removeprefix('RsRole').lower():True},examples=[],notes=[]))
for ident,label,syntax,provider in re.findall(r'\{"([^"\n]+)", "([^"\n]+)", "([^"\n]+)", "([^"\n]+)"\}',catalogue):
    for definition in definitions:
        if definition['id']==ident: definition['examples'].append(dict(label=label,syntax=syntax,provider=provider))
for ident,note in re.findall(r'\{"([^"\n]+)", "([^"\n]+)"\}',catalogue):
    for definition in definitions:
        if definition['id']==ident: definition['notes'].append(note)
(preview/'catalogue.js').write_text('window.demoCommands='+json.dumps(definitions)+';',encoding='utf-8')
for name in pages:
    html = (source / 'src/music_player' / (name + '.html')).read_text(encoding='utf-8')
    html = html.replace('https://app.rearsilver.test/Sora-Variable.ttf', 'Sora-Variable.ttf')
    html = html.replace('<head>', '<head><script>window.rsNativePost=()=>{};</script>', 1)
    if name == 'library':
        html = html.replace("<script>(()=>{", "<script>(()=>{const external=document.getElementById('external'),status=document.getElementById('status');", 1)
    html = html.replace('</body>', '<script src="catalogue.js"></script><script src="demo.js"></script></body>')
    (preview / (name + '.html')).write_text(html, encoding='utf-8')
for path in (root / 'native-originals').glob('*.png'):
    with Image.open(path) as im:
        im.crop((248,32,1090,600)).save(root / 'native' / path.name)
print('Prepared six interface pages and cropped native captures.')
shutil.copy2(root/'demo.js',preview/'demo.js')
