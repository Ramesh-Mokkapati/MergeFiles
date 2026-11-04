"""Regenerates the Phase 6 sample files in testdata\\documents and testdata\\archives.

    python testdata\\make_samples.py

Deterministic: fixed timestamps, so the files only change when this script does. The unit tests
(tests\\DocumentTests.cpp) check the text MergeFiles extracts from these files.
"""
import gzip
import io
import os
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(HERE, 'documents')
ARCH = os.path.join(HERE, 'archives')
STAMP = (2026, 1, 15, 10, 30, 0)
EPOCH = 1768473000  # 2026-01-15 10:30:00 UTC


def write_zip(path, parts, level=9, stored=()):
    with zipfile.ZipFile(path, 'w') as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, STAMP)
            if name in stored or name == 'mimetype':
                info.compress_type = zipfile.ZIP_STORED
            else:
                info.compress_type = zipfile.ZIP_DEFLATED
            if isinstance(data, str):
                data = data.encode('utf-8')
            z.writestr(info, data, compresslevel=level)


# ---------------- Word ----------------

W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"'
DOCX_TYPES = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
              '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="xml" ContentType="application/xml"/>'
              '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
              '</Types>')
DOCX_RELS = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
             '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
             '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>'
             '</Relationships>')


def para(*runs):
    return '<w:p>' + ''.join(runs) + '</w:p>'


def run(text, bold=False):
    props = '<w:rPr><w:b/></w:rPr>' if bold else ''
    return '<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>' % (props, text)


def cell(text):
    return '<w:tc><w:tcPr/>' + para(run(text)) + '</w:tc>'


def docx(path, percent, paper, extra_row, extra_para):
    rows = [('Item', 'Cost'), ('Paper', paper)] + ([extra_row] if extra_row else [])
    table = '<w:tbl><w:tblPr/>' + ''.join('<w:tr>' + cell(a) + cell(b) + '</w:tr>' for a, b in rows) + '</w:tbl>'
    body = (
        '<w:p><w:pPr><w:pStyle w:val="Title"/><w:tabs><w:tab w:val="left" w:pos="720"/></w:tabs></w:pPr>' + run('Quarterly report') + '</w:p>'
        + para(run('Sales rose by '), run(percent, True), run(' in the third quarter.'))
        + para(run('Region'), '<w:r><w:tab/></w:r>', run('North &amp; South'))
        + table
        + para(run('Closing remarks.'), '<w:del w:id="1" w:author="A"><w:r><w:delText>Deleted words.</w:delText></w:r></w:del>')
        + (para(run(extra_para)) if extra_para else '')
        + '<w:sectPr/>')
    document = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document %s><w:body>%s</w:body></w:document>' % (W, body)
    write_zip(path, [('[Content_Types].xml', DOCX_TYPES), ('_rels/.rels', DOCX_RELS), ('word/document.xml', document)])


# ---------------- Excel ----------------

XLSX_TYPES = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
              '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="xml" ContentType="application/xml"/>'
              '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
              '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
              '<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
              '<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>'
              '</Types>')
XLSX_RELS = DOCX_RELS.replace('word/document.xml', 'xl/workbook.xml')
S = 'xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'


def xlsx(path, paper):
    workbook = ('<workbook %s><sheets><sheet name="Budget" sheetId="1" r:id="rId1"/><sheet name="Notes" sheetId="2" r:id="rId2"/></sheets></workbook>' % S)
    rels = ('<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
            '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet2.xml"/>'
            '<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>'
            '</Relationships>')
    shared = ('<sst %s count="4" uniqueCount="4"><si><t>Item</t></si><si><t>Cost</t></si>'
              '<si><r><t>Pa</t></r><r><t>per</t></r><rPh sb="0" eb="1"><t>PHONETIC</t></rPh></si><si><t>Total</t></si></sst>' % S)
    sheet1 = ('<worksheet %s><sheetData>'
              '<row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row>'
              '<row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2"><v>%s</v></c></row>'
              '<row r="4"><c r="A4" t="s"><v>3</v></c><c r="B4"><f>SUM(B2:B3)</f><v>%s</v></c><c r="D4" t="inlineStr"><is><t>note</t></is></c></row>'
              '</sheetData></worksheet>' % (S, paper, paper))
    sheet2 = ('<worksheet %s><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>Done</t></is></c><c r="B1" t="b"><v>1</v></c></row></sheetData></worksheet>' % S)
    write_zip(path, [('[Content_Types].xml', XLSX_TYPES), ('_rels/.rels', XLSX_RELS), ('xl/workbook.xml', workbook),
                     ('xl/_rels/workbook.xml.rels', rels), ('xl/sharedStrings.xml', shared),
                     ('xl/worksheets/sheet1.xml', sheet1), ('xl/worksheets/sheet2.xml', sheet2)])


# ---------------- PowerPoint ----------------

def pptx(path):
    ns = ('xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
          'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"')
    presentation = '<p:presentation %s><p:sldIdLst><p:sldId id="256" r:id="rId2"/><p:sldId id="257" r:id="rId1"/></p:sldIdLst></p:presentation>' % ns
    rels = ('<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide2.xml"/>'
            '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" Target="slides/slide1.xml"/>'
            '</Relationships>')

    def slide(paragraphs):
        body = ''.join('<a:p><a:r><a:rPr lang="en-US"/><a:t>%s</a:t></a:r></a:p>' % t for t in paragraphs)
        return '<p:sld %s><p:cSld><p:spTree><p:sp><p:txBody><a:bodyPr/>%s</p:txBody></p:sp></p:spTree></p:cSld></p:sld>' % (ns, body)

    types = DOCX_TYPES.replace('/word/document.xml', '/ppt/presentation.xml').replace('wordprocessingml.document.main', 'presentationml.presentation.main')
    write_zip(path, [('[Content_Types].xml', types), ('_rels/.rels', DOCX_RELS.replace('word/document.xml', 'ppt/presentation.xml')),
                     ('ppt/presentation.xml', presentation), ('ppt/_rels/presentation.xml.rels', rels),
                     ('ppt/slides/slide1.xml', slide(['Welcome', 'Agenda item'])), ('ppt/slides/slide2.xml', slide(['Thanks']))])


# ---------------- OpenDocument ----------------

ODF_NS = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
          'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" office:version="1.2"')


def odf(path, mimetype, body):
    content = '<?xml version="1.0" encoding="UTF-8"?>\n<office:document-content %s>\n<office:body>\n%s\n</office:body>\n</office:document-content>\n' % (ODF_NS, body)
    manifest = ('<?xml version="1.0" encoding="UTF-8"?><manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.2">'
                '<manifest:file-entry manifest:full-path="/" manifest:media-type="%s"/>'
                '<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/></manifest:manifest>' % mimetype)
    write_zip(path, [('mimetype', mimetype), ('content.xml', content), ('META-INF/manifest.xml', manifest)])


def odt(path, day):
    body = ('<office:text>\n'
            '  <text:h text:outline-level="1">Meeting notes</text:h>\n'
            '  <text:p>Attendees:<text:s text:c="2"/>Ann,   Bob<text:tab/>(remote)</text:p>\n'
            '  <text:p>Decided <text:span>to ship</text:span> on %s.<office:annotation><text:p>Hidden comment</text:p></office:annotation></text:p>\n'
            '  <table:table table:name="T1"><table:table-column table:number-columns-repeated="2"/>\n'
            '    <table:table-row><table:table-cell><text:p>Task</text:p></table:table-cell><table:table-cell><text:p>Owner</text:p></table:table-cell></table:table-row>\n'
            '    <table:table-row><table:table-cell><text:p>Docs</text:p><text:p>and help</text:p></table:table-cell><table:table-cell><text:p>Ann</text:p></table:table-cell></table:table-row>\n'
            '  </table:table>\n'
            '</office:text>' % day)
    odf(path, 'application/vnd.oasis.opendocument.text', body)


def ods(path):
    body = ('<office:spreadsheet><table:table table:name="Budget">'
            '<table:table-column table:number-columns-repeated="1024"/>'
            '<table:table-row><table:table-cell><text:p>Item</text:p></table:table-cell><table:table-cell><text:p>Cost</text:p></table:table-cell>'
            '<table:table-cell table:number-columns-repeated="1022"/></table:table-row>'
            '<table:table-row><table:table-cell><text:p>Paper</text:p></table:table-cell><table:table-cell table:number-columns-repeated="2"/>'
            '<table:table-cell office:value-type="float" office:value="40"><text:p>40</text:p></table:table-cell>'
            '<table:table-cell table:number-columns-repeated="1020"/></table:table-row>'
            '<table:table-row table:number-rows-repeated="1048574"><table:table-cell table:number-columns-repeated="1024"/></table:table-row>'
            '</table:table></office:spreadsheet>')
    odf(path, 'application/vnd.oasis.opendocument.spreadsheet', body)


# ---------------- RTF ----------------

def rtf(path, price):
    text = ('{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0 Calibri;}}{\\colortbl;\\red0\\green0\\blue0;}{\\*\\generator Test;}\n'
            '\\pard Caf\\\'e9 menu\\par\n'
            'Price: %s\\tab EUR\\par\n'
            '{\\*\\comment hidden}Unicode: \\u8364?uro\\par\n'
            '\\trowd\\cellx1000\\cellx2000\\pard\\intbl Tea\\cell 2\\cell\\row\n'
            '\\pard Last line\\par}\n' % price)
    with open(path, 'w', newline='\r\n') as f:
        f.write(text)


# ---------------- archives ----------------

def folder_files(folder):
    result = []
    for root, dirs, files in os.walk(folder):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            result.append((os.path.relpath(full, folder).replace('\\', '/'), open(full, 'rb').read()))
    return result


def big_text():
    return ''.join('Line %05d: the quick brown fox jumps over the lazy dog %d\n' % (i, i * 7 % 13) for i in range(1, 5001))


def archives():
    folders = os.path.join(HERE, 'folders')
    # left.zip: testdata\folders\left; right.tar.gz: testdata\folders\right.
    write_zip(os.path.join(ARCH, 'left.zip'), folder_files(os.path.join(folders, 'left')))
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w', format=tarfile.GNU_FORMAT) as t:
        for name, data in folder_files(os.path.join(folders, 'right')):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = EPOCH
            t.addfile(info, io.BytesIO(data))
    with open(os.path.join(ARCH, 'right.tar.gz'), 'wb') as f:
        with gzip.GzipFile(fileobj=f, mode='wb', mtime=0, filename='right.tar') as g:
            g.write(raw.getvalue())

    # mixed.zip: a large deflated file (dynamic Huffman, several blocks), a stored one, a UTF-8 name, an explicit folder.
    with zipfile.ZipFile(os.path.join(ARCH, 'mixed.zip'), 'w') as z:
        for name, data, method in [('data/', b'', zipfile.ZIP_STORED), ('data/big.txt', big_text().encode(), zipfile.ZIP_DEFLATED),
                                   ('stored.txt', b'kept as is\n', zipfile.ZIP_STORED), ('café.txt', 'naïve\n'.encode(), zipfile.ZIP_DEFLATED),
                                   ('fixed.txt', b'abcabcabcabc\n', zipfile.ZIP_DEFLATED)]:
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = method
            z.writestr(info, data, compresslevel=9)

    # mixed.tar: a GNU long name and a PAX (non-ASCII) name.
    long_name = 'deep/' + 'very_long_folder_name_' * 5 + '/file.txt'
    with tarfile.open(os.path.join(ARCH, 'mixed.tar'), 'w', format=tarfile.PAX_FORMAT) as t:
        for name, data in [(long_name, b'long\n'), ('über.txt', b'umlaut\n'), ('empty/', None)]:
            info = tarfile.TarInfo(name.rstrip('/'))
            info.mtime = EPOCH
            if data is None:
                info.type = tarfile.DIRTYPE
                t.addfile(info)
            else:
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))


def main():
    os.makedirs(DOCS, exist_ok=True)
    os.makedirs(ARCH, exist_ok=True)
    docx(os.path.join(DOCS, 'report_v1.docx'), '12%', '40', None, None)
    docx(os.path.join(DOCS, 'report_v2.docx'), '15%', '45', ('Ink', '30'), 'Next review in May.')
    # The same text as report_v1.docx in a differently compressed package.
    write_zip_level = write_zip
    def docx_level(path):
        global write_zip
        saved = write_zip
        write_zip = lambda p, parts, level=1, stored=(): saved(p, parts, level=1, stored=stored)
        try:
            docx(path, '12%', '40', None, None)
        finally:
            write_zip = saved
    docx_level(os.path.join(DOCS, 'report_v1_resaved.docx'))
    del write_zip_level
    xlsx(os.path.join(DOCS, 'budget_v1.xlsx'), '40')
    xlsx(os.path.join(DOCS, 'budget_v2.xlsx'), '42')
    pptx(os.path.join(DOCS, 'slides.pptx'))
    odt(os.path.join(DOCS, 'notes_v1.odt'), 'Friday')
    odt(os.path.join(DOCS, 'notes_v2.odt'), 'Monday')
    ods(os.path.join(DOCS, 'budget.ods'))
    rtf(os.path.join(DOCS, 'menu_v1.rtf'), '5')
    rtf(os.path.join(DOCS, 'menu_v2.rtf'), '6')
    archives()


if __name__ == '__main__':
    main()
