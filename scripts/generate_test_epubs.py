"""Generate small original EPUB fixtures for native tests; contains no published book text."""
from pathlib import Path
import html
import zipfile


def write_epub(path, title, paragraphs):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as out:
        def put(name, content):
            entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            out.writestr(entry, content)
        put('mimetype', 'application/epub+zip')
        put('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        put('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + html.escape(title) + '</dc:title><dc:identifier id="id">cross-native-test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        put('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + html.escape(title) + '</title></head><body>' + ''.join('<p>' + html.escape(p) + '</p>' for p in paragraphs) + '</body></html>')


if __name__ == '__main__':
    target = Path(__file__).resolve().parents[1] / 'test/epubs'
    text = 'Office affine affinity. The reader turns one page and keeps its position. Clear lines fit the page. '
    write_epub(target / 'test_kerning_ligature.epub', 'Native layout fixture', [text * 8] * 24)
    write_epub(target / 'test_dictionary_synonyms.epub', 'Synonym Lookup Test', ['Up and confirm select a word in this original test paragraph.'] + [text * 8] * 24)
    print('Generated two deterministic, original test EPUBs')
