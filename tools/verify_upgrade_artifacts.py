"""Verify the saved actual-data fixture artifacts; this is a developer QA tool."""
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image, ImageChops
import pypdfium2 as pdfium

root = Path(__file__).resolve().parents[1]
folder = root / 'outputs/upgrade-validation'
model = json.loads((folder / 'report-model.json').read_text(encoding='utf-8'))
source = json.loads((folder / 'baseline-model.json').read_text(encoding='utf-8'))
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
metrics = dict(groups=0, pngs=0, gifs=0, frames=0, unchanged_input_files=0)
expected_tables = []
sum_corrections = []
for before, after in zip(source['sections'], model['sections']):
    assert before['project_id'] == after['project_id']
    for old_row, new_row in zip(before['rows'], after['rows']):
        assert old_row['sheet_row'] == new_row['sheet_row']
        assert old_row['cells'] == new_row['cells']
        for kind in ['primary_source', 'bb_source']:
            info = old_row.get(kind, {})
            if info.get('status') == 'found':
                assert hashlib.sha256(Path(info['local_path']).read_bytes()).hexdigest() == info['sha256']
                metrics['unchanged_input_files'] += 1
        if 'analysis' in old_row and 'analysis' in new_row:
            assert old_row['analysis']['xyz'] == new_row['analysis']['xyz'], 'Rendering must not mutate analysis coordinates'
    for old, group in zip(before['analyses'], after['analyses']):
        metrics['groups'] += 1
        for name in ['energy_table', 'geometry_table']:
            assert old[name]['headers'] == group[name]['headers']
            assert old[name]['rows'] == group[name]['rows']
        for old_row, new_row in zip(old['spin_table']['rows'], group['spin_table']['rows']):
            if old_row != new_row:
                assert new_row[0] in ['Rest', 'Sum']
                assert all(v is None for v in new_row[1:])
                sum_corrections.append(dict(group=group['section'], label=new_row[0], before=old_row[1:], after=new_row[1:]))
        for name in ['energy_table', 'spin_table', 'geometry_table']:
            expected_tables.append((name, group[name]))
        captures = group['captures']
        assert len(captures) == 3
        assert all(c['camera'] == captures[0]['camera'] and c['crop'] == captures[0]['crop'] for c in captures)
        assert len({(c['png_width_px'], c['png_height_px']) for c in captures}) == 1
        for transform in group['group_view']['transforms']:
            a, b, c, d, e, f, g, h, i = [transform[k] for k in [0, 1, 2, 4, 5, 6, 8, 9, 10]]
            determinant = a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
            assert abs(determinant - 1) < 1e-4
        for capture in captures:
            png = Image.open(capture['png']); png.load(); metrics['pngs'] += 1
            assert abs(png.info['dpi'][0] - 600) < 1
            if 'animation' in capture:
                gif = Image.open(capture['animation']); assert gif.n_frames == 32; metrics['gifs'] += 1
                for frame in range(gif.n_frames):
                    gif.seek(frame); image = gif.convert('RGB')
                    assert image.size == (capture['animation_width_px'], capture['animation_height_px'])
                    # Non-white content remains inside the common crop, including every frame.
                    diff = ImageChops.difference(image, Image.new('RGB', image.size, 'white'))
                    mask = diff.convert('L').point(lambda p: 255 if p >= 4 else 0)
                    box = mask.getbbox()
                    assert box and min(box[0], box[1], image.width-box[2], image.height-box[3]) >= 2
                    metrics['frames'] += 1
        for paragraph in group['paragraphs']:
            assert not re.search(r'\{[^{}]+\}|~~|작성해주세요|시사합니다|유의미한', paragraph['text'])

def rendered_cell(value, digits, logical=None):
    if value is None:
        return 'N/A'
    if isinstance(value, str):
        return value
    if logical is None and abs(value) < 0.5 * 10**-digits:
        value = 0
    result = f'{value:.{digits}f}'
    if ((logical in [1,3,4,5] and value > 0) or (logical in [6,7] and value >= 0)) and not result.startswith('-'):
        result = '+' + result
    return result

with zipfile.ZipFile(folder / 'Youngkly-upgraded-report.docx') as archive:
    doc = ET.fromstring(archive.read('word/document.xml'))
    tables = doc.findall('.//w:tbl', ns); assert len(tables) == len(expected_tables) == 12
    for actual, (kind, expected) in zip(tables, expected_tables):
        rows = actual.findall('w:tr', ns); assert len(rows) == len(expected['rows']) + 1
        assert len(rows[0].findall('w:tc', ns)) == len(expected['headers'])
        for row, values in zip(rows[1:], expected['rows']):
            text = [''.join(t.text or '' for t in cell.findall('.//w:t', ns)) for cell in row.findall('w:tc', ns)]
            want = [rendered_cell(v, 2 if kind == 'energy_table' else 3,
                     expected['logical_columns'][i] if kind == 'energy_table' else None) for i, v in enumerate(values)]
            assert text == want, (kind, text, want)
    texts = [''.join(t.text or '' for t in p.findall('.//w:t', ns)) for p in doc.find('w:body', ns).findall('w:p', ns)]
    future = max(i for i, t in enumerate(texts) if t == 'Future work')
    assert not any(texts[future+1:]), 'No current confirmed plan: do not invent Future work'

with zipfile.ZipFile(folder / 'Future-work-reference-example.docx') as archive:
    doc = ET.fromstring(archive.read('word/document.xml'))
    text = ''.join(t.text or '' for t in doc.findall('.//w:t', ns))
    plan = json.loads((folder / 'reference-plans.json').read_text(encoding='utf-8'))['items'][0]
    assert plan['next_action'] in text and '2026년 10월 14일' in text

pages = {}
for name in ['Youngkly-upgraded-report', 'Future-work-reference-example']:
    pdf = pdfium.PdfDocument(folder / (name + '.pdf')); pages[name] = len(pdf)
    for index, page in enumerate(pdf):
        page.render(scale=1.5).to_pil().save(folder / f'{name}-page-{index+1:02}.png')
metrics['pages'] = pages
metrics['explicit_sum_corrections'] = sum_corrections
(folder / 'artifact-checks.json').write_text(json.dumps(metrics, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(metrics, ensure_ascii=False, indent=2))
