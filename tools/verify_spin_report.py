"""Verify a rendered spin-comparison report against its model (bundled Python)."""
import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from zipfile import ZipFile

parser = argparse.ArgumentParser()
parser.add_argument('model', type=Path)
parser.add_argument('report', type=Path)
args = parser.parse_args()
model = json.loads(args.model.read_text(encoding='utf-8'))
groups = [g for s in model.get('sections', [model]) for g in s.get('analyses', [])]
ready = [g for g in groups if g['status'] != 'missing_outputs']
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
      'wp': 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing'}
with ZipFile(args.report) as z:
    for name in z.namelist():
        if name.endswith(('.xml', '.rels')):
            ET.fromstring(z.read(name))
    root = ET.fromstring(z.read('word/document.xml'))
    tables = root.findall('.//w:tbl', ns)
    assert len(tables) == len(ready) * 3
    assert len(root.findall('.//wp:extent', ns)) == sum(len(g['rows']) for g in ready)
    assert not root.findall('.//w:tbl//w:highlight', ns)
    for index, group in enumerate(ready):
        assert len(group['captures']) == len(group['rows'])
        if group.get('kind') == 'spin_optimization':
            assert all('animation' not in capture for capture in group['captures'])
            assert all('barrier' not in p for p in group.get('paragraphs', []))
            assert not any('TS' in p['text'] or '경로 중' in p['text'] for p in group.get('paragraphs', []))
        for offset, key in enumerate(('energy_table', 'spin_table', 'geometry_table')):
            table = group[key]
            rows = tables[index * 3 + offset].findall('w:tr', ns)
            assert len(rows) == len(table['rows']) + 1
            for row in rows:
                assert len(row.findall('w:tc', ns)) == len(table['headers'])
            for column, header in zip(rows[0].findall('w:tc', ns), table['headers']):
                text = ''.join(t.text or '' for t in column.findall('.//w:t', ns))
                if key != 'energy_table':
                    assert text == header, (text, header)
    for g in groups:
        if g['status'] == 'missing_outputs':
            assert g['section'] in ''.join(t.text or '' for t in root.findall('.//w:t', ns))
print(f'Verified {len(ready)} comparisons, {len(tables)} editable tables, '
      f'{sum(len(g["rows"]) for g in ready)} structures')
