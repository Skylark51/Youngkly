"""Developer QA only: run with the bundled workspace Python runtime."""
import argparse
import hashlib
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('reference', type=Path)
parser.add_argument('report', type=Path)
args = parser.parse_args()
editable = {'word/document.xml', 'word/_rels/document.xml.rels',
            '[Content_Types].xml', 'word/settings.xml'}
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
      'wp': 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing'}
with zipfile.ZipFile(args.reference) as source, zipfile.ZipFile(args.report) as output:
    assert set(source.namelist()).issubset(output.namelist())
    for name in source.namelist():
        if name not in editable:
            assert source.read(name) == output.read(name), name
    for name in output.namelist():
        if name.endswith('.xml') or name.endswith('.rels'):
            ET.fromstring(output.read(name))
    original = source.read('word/document.xml')
    document = output.read('word/document.xml')
    section = re.compile(rb'<w:sectPr\b.*?</w:sectPr>', re.S)
    assert section.findall(original) == section.findall(document)
    root = ET.fromstring(document)
    reference_body = ET.fromstring(original).find('w:body', ns)
    output_body = root.find('w:body', ns)
    for index in [0, 1, 2, 4, 5]:
        paragraph_text = lambda node: ''.join(e.text or '' for e in node.findall('.//w:t', ns))
        assert paragraph_text(reference_body[index]) == paragraph_text(output_body[index]), 'Cover text spacing'
    tables = root.findall('.//w:tbl', ns)
    assert len(tables) % 3 == 0
    triplets = len(tables) // 3
    assert not root.findall('.//w:tbl//w:highlight', ns)
    # Highlights are optional when no confirmed changed observations exist.
    assert all(e.attrib['cx'] == '1800000' for e in root.findall('.//wp:extent', ns))
    assert len(root.findall('.//wp:extent', ns)) == 3 * triplets
    for table in tables:
        assert all(p.find('w:pPr/w:jc', ns).get('{'+ns['w']+'}val') == 'center'
                   for p in table.findall('.//w:p', ns))
    body_children = list(output_body)
    def blank(node):
        return node.tag == '{'+ns['w']+'}p' and not ''.join(x.text or '' for x in node.findall('.//w:t', ns)) and not node.findall('.//w:drawing', ns)
    table_positions = [i for i, e in enumerate(body_children) if e.tag == '{'+ns['w']+'}tbl']
    for i in table_positions:
        assert blank(body_children[i-1]) and blank(body_children[i+1]), 'One line between table and text'
    for i in table_positions:
        gap = body_children[i+1].find('w:pPr/w:spacing', ns)
        assert gap is not None and gap.get('{'+ns['w']+'}lineRule') == 'exact', 'Explicit point-based table gap'
    for table in tables:
        assert table.find('w:tblPr/w:tblBorders/w:top', ns).get('{'+ns['w']+'}sz') == '6'
        for row in table.findall('w:tr', ns)[1:]:
            assert not row.findall('.//w:keepNext', ns), 'Body rows must not form an unbreakable table'
    for p in output_body.findall('w:p', ns):
        value = ''.join(e.text or '' for e in p.findall('.//w:t', ns))
        if value == 'Future work' and p.find('w:pPr/w:pStyle', ns).get('{'+ns['w']+'}val') == '2':
            assert p.find('w:pPr/w:pageBreakBefore', ns) is not None
    for run in root.findall('.//w:r', ns):
        assert run.find('w:rPr/w:rFonts', ns).get('{'+ns['w']+'}eastAsia') == '맑은 고딕'
    for index in range(triplets):
        spin = tables[index*3+1].findall('w:tr', ns)[1]
        assert re.match(r'(Co|Mn|Fe|Ni|Cu|Cr|V|Ti|Mo|Ru|Ir|Rh|Pd|Pt|Zn)\d', ''.join(e.text or '' for e in spin.findall('.//w:t', ns)))
        spin_end = table_positions[index*3+1]
        next_table = table_positions[index*3+2]
        assert any('spin density는' in ''.join(x.text or '' for x in p.findall('.//w:t', ns))
                   for p in body_children[spin_end+1:next_table]), 'Spin narrative below spin table'
    gifs = [name for name in output.namelist() if name.startswith('word/media/youngkly-') and name.endswith('.gif')]
    assert len(gifs) == triplets
    import io
    for gif in gifs:
        image = Image.open(io.BytesIO(output.read(gif)))
        assert image.n_frames == 32
        for index in range(image.n_frames):
            image.seek(index)
            image.load()
print('OOXML, native tables, Korean fonts, alignment, paragraph gaps, spin narratives, Future work page break and GIF frames verified')
print('Reference SHA256:', hashlib.sha256(args.reference.read_bytes()).hexdigest())
