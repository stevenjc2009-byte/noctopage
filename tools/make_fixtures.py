#!/usr/bin/env python3
"""Generate the Noctopage test fixtures into tests/fixtures/ (deterministic, < 2 MB total).

    python tools/make_fixtures.py [--out DIR]

Needs Pillow (PNG/JPEG/WebP). Everything else is stdlib: zipfile, tarfile, hand-written PDF.
Skipped on purpose: CBR (needs the proprietary rar writer) and CB7 (needs py7zr or 7z.exe;
the script writes a CB7 only if one of them is found).
Re-running overwrites; output bytes are stable (fixed timestamps) so diffs stay quiet.
"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, features

ZIP_TIME = (2026, 1, 1, 0, 0, 0)
HERE = Path(__file__).resolve().parent
DEFAULT_OUT = HERE.parent / "tests" / "fixtures"

PALETTE = [(200, 60, 60), (60, 160, 90), (60, 90, 200), (210, 170, 50), (150, 70, 190),
           (40, 170, 180), (230, 120, 40), (110, 110, 110), (180, 60, 140), (90, 140, 60)]


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:  # Pillow < 10.1 has no sized default font
        return ImageFont.load_default()


def page_image(label, idx, size=(240, 360), fmt="PNG", quality=80):
    """A page with a big readable label, so a test (or a human) can tell which page it is."""
    img = Image.new("RGB", size, PALETTE[idx % len(PALETTE)])
    d = ImageDraw.Draw(img)
    d.rectangle([8, 8, size[0] - 9, size[1] - 9], outline=(255, 255, 255), width=4)
    f = font(size[0] // 3)
    d.text((size[0] // 2, size[1] // 2), label, fill=(255, 255, 255), font=f, anchor="mm")
    buf = io.BytesIO()
    if fmt == "JPEG":
        img.save(buf, "JPEG", quality=quality)
    elif fmt == "WEBP":
        img.save(buf, "WEBP", quality=quality)
    else:
        img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def zinfo(name, stored=False):
    zi = zipfile.ZipInfo(name, date_time=ZIP_TIME)
    zi.compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    return zi


def write_zip(path, entries):
    """entries: list of (name, bytes, stored?) written in exactly this order."""
    with zipfile.ZipFile(path, "w") as z:
        for name, data, stored in entries:
            z.writestr(zinfo(name, stored), data)


def make_cbz_natural(out, manifest):
    # Natural order is p1 p2 p3 p9 p10; the archive stores them in plain lexicographic order
    # (p1 p10 p2 p3 p9), so a reader that forgets natural sort shows page 10 second.
    nums = [1, 2, 3, 9, 10]
    stored_order = sorted(nums, key=str)
    entries = [("p%d.png" % n, page_image("p%d" % n, n), True) for n in stored_order]
    write_zip(out / "natural_order.cbz", entries)
    manifest["natural_order.cbz"] = {
        "kind": "cbz", "pages": 5,
        "archive_order": ["p%d.png" % n for n in stored_order],
        "natural_order": ["p%d.png" % n for n in sorted(nums)],
    }
    return entries


def make_cbz_mixed(out, manifest):
    # Cover + case variations + junk that a page list must drop.
    names = [("Cover.JPG", "JPEG", 0, "C"), ("page2.PNG", "PNG", 2, "2"),
             ("Page10.png", "PNG", 3, "10"), ("page1.png", "PNG", 1, "1")]
    entries = [(n, page_image(label, i, fmt=f), True) for n, f, i, label in names]
    entries.insert(1, ("__MACOSX/._page1.png", b"\x00\x05\x16\x07junk", True))
    entries.insert(2, ("Thumbs.db", b"not an image", True))
    entries.append(("notes/readme.txt", b"scanlation notes\n", False))
    write_zip(out / "mixed_case_junk.cbz", entries)
    manifest["mixed_case_junk.cbz"] = {
        "kind": "cbz", "pages": 4,
        "natural_order": ["Cover.JPG", "page1.png", "page2.PNG", "Page10.png"],
        "must_ignore": ["__MACOSX/._page1.png", "Thumbs.db", "notes/readme.txt"],
    }


def make_cbt(out, manifest, pages):
    path = out / "natural_order.cbt"
    with tarfile.open(path, "w", format=tarfile.USTAR_FORMAT) as t:
        for name, data, _ in pages:
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            ti.mtime = 1767225600  # 2026-01-01
            ti.mode = 0o644
            t.addfile(ti, io.BytesIO(data))
    manifest["natural_order.cbt"] = dict(manifest["natural_order.cbz"], kind="cbt")


def make_cb7(out, manifest):
    """Only if a 7z writer exists. Returns True if written."""
    src = out / "natural_order.cbz"
    names = manifest["natural_order.cbz"]["archive_order"]
    dst = out / "natural_order.cb7"
    try:
        import py7zr  # type: ignore
        with py7zr.SevenZipFile(dst, "w") as z:
            with zipfile.ZipFile(src) as zf:
                for n in names:
                    z.writestr(zf.read(n), n)
    except ImportError:
        exe = shutil.which("7z") or shutil.which("7za") or shutil.which("7zr")
        for cand in (r"C:\Program Files\7-Zip\7z.exe", r"C:\Program Files (x86)\7-Zip\7z.exe"):
            if not exe and Path(cand).exists():
                exe = cand
        if not exe:
            return False
        tmp = out / "_cb7_tmp"
        tmp.mkdir(exist_ok=True)
        try:
            with zipfile.ZipFile(src) as zf:
                zf.extractall(tmp)
            for f in tmp.iterdir():
                os.utime(f, (1767225600, 1767225600))  # fixed mtime keeps the .cb7 bytes stable
            if dst.exists():
                dst.unlink()
            subprocess.run([exe, "a", "-t7z", str(dst), "*"], cwd=tmp, check=True,
                           stdout=subprocess.DEVNULL)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    manifest["natural_order.cb7"] = dict(manifest["natural_order.cbz"], kind="cb7")
    return True


def make_folder_comic(out, manifest):
    d = out / "folder_comic"
    d.mkdir(exist_ok=True)
    files = [("page1.jpg", "JPEG", 1, "1"), ("page2.png", "PNG", 2, "2"),
             ("page10.jpg", "JPEG", 3, "10")]
    for name, fmt, i, label in files:
        (d / name).write_bytes(page_image(label, i, fmt=fmt))
    manifest["folder_comic/"] = {"kind": "folder", "pages": 3,
                                 "natural_order": [f[0] for f in files]}


def make_tall(out, manifest):
    """800x9000: taller than the 4096 texture limit, so it must be tiled. A coloured band and a
    label every 1000 px lets a test (or a human) see which tile it is looking at."""
    w, h = 800, 9000
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    f = font(120)
    for i in range(9):
        d.rectangle([0, i * 1000, w, (i + 1) * 1000], fill=PALETTE[i % len(PALETTE)])
        d.text((w // 2, i * 1000 + 500), "y=%d" % (i * 1000), fill=(255, 255, 255), font=f,
               anchor="mm")
    d.rectangle([0, 0, w - 1, h - 1], outline=(255, 255, 255), width=6)
    img.save(out / "tall_800x9000.png", "PNG", optimize=True)
    img.save(out / "tall_800x9000.jpg", "JPEG", quality=70)
    manifest["tall_800x9000.png"] = {"kind": "image", "width": w, "height": h}
    manifest["tall_800x9000.jpg"] = {"kind": "image", "width": w, "height": h}


def make_webp(out, manifest):
    if not features.check("webp"):
        return False
    (out / "page.webp").write_bytes(page_image("W", 4, size=(400, 600), fmt="WEBP"))
    manifest["page.webp"] = {"kind": "image", "width": 400, "height": 600}
    return True


def make_japanese_name(out, manifest):
    names = ["ページ01.png", "ページ02.png", "ページ10.png"]
    entries = [(n, page_image(str(i + 1), i + 5), True) for i, n in enumerate(names)]
    name = "日本語の漫画 第1巻.cbz"
    write_zip(out / name, entries)
    manifest[name] = {"kind": "cbz", "pages": 3, "natural_order": names, "utf8_entry_names": True}


CH1 = [
    "It was a bright cold day in April, and the clocks were striking thirteen.",
    "The library at the end of the lane kept its doors open late, and a lamp burned in the "
    "window of the reading room for anyone who had nowhere else to be.",
    "She carried the small book under her arm all the way from the station, past the bakery, "
    "past the shuttered tailor, and up the three worn steps.",
]
CH2 = [
    "The second chapter begins with rain. It came sideways off the harbour and found every "
    "gap in every coat.",
    "By evening the street was a river of lamplight. Somewhere a window closed; somewhere a "
    "kettle began to sing.",
    "Quotes, dashes and accents should survive: \u201cNa\u00efve caf\u00e9 \u2014 r\u00e9sum\u00e9,\u201d "
    "she said, and then in Japanese: \u3053\u3093\u306b\u3061\u306f\u3001\u4e16\u754c\u3002",
]


def xhtml(title, paras):
    body = "\n".join("    <p>%s</p>" % p for p in paras)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" '
            'xml:lang="en" lang="en">\n<head><meta charset="utf-8"/><title>%s</title></head>\n'
            '<body>\n    <h1>%s</h1>\n%s\n</body>\n</html>\n' % (title, title, body)).encode("utf-8")


def make_epub(out, manifest):
    container = (b'<?xml version="1.0" encoding="UTF-8"?>\n'
                 b'<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
                 b'  <rootfiles>\n    <rootfile full-path="OEBPS/content.opf" '
                 b'media-type="application/oebps-package+xml"/>\n  </rootfiles>\n</container>\n')
    opf = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid" '
           'xml:lang="en">\n'
           '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
           '    <dc:identifier id="bookid">urn:uuid:5f0a3b7e-4c1d-4e0a-9d52-6b1f0c2a7e10</dc:identifier>\n'
           '    <dc:title>Fixture Book</dc:title>\n'
           '    <dc:creator>Noctopage Tests</dc:creator>\n'
           '    <dc:language>en</dc:language>\n'
           '    <meta property="dcterms:modified">2026-01-01T00:00:00Z</meta>\n'
           '  </metadata>\n'
           '  <manifest>\n'
           '    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>\n'
           '    <item id="cover-img" href="images/cover.png" media-type="image/png" '
           'properties="cover-image"/>\n'
           '    <item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/>\n'
           '    <item id="ch2" href="ch2.xhtml" media-type="application/xhtml+xml"/>\n'
           '  </manifest>\n'
           '  <spine>\n    <itemref idref="ch1"/>\n    <itemref idref="ch2"/>\n  </spine>\n'
           '</package>\n').encode("utf-8")
    nav = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" '
           'xml:lang="en" lang="en">\n<head><meta charset="utf-8"/><title>Contents</title></head>\n'
           '<body>\n  <nav epub:type="toc" id="toc">\n    <h1>Contents</h1>\n    <ol>\n'
           '      <li><a href="ch1.xhtml">Chapter One</a></li>\n'
           '      <li><a href="ch2.xhtml">Chapter Two</a></li>\n'
           '    </ol>\n  </nav>\n</body>\n</html>\n').encode("utf-8")
    entries = [
        ("mimetype", b"application/epub+zip", True),  # must be first and stored
        ("META-INF/container.xml", container, False),
        ("OEBPS/content.opf", opf, False),
        ("OEBPS/nav.xhtml", nav, False),
        ("OEBPS/ch1.xhtml", xhtml("Chapter One", CH1), False),
        ("OEBPS/ch2.xhtml", xhtml("Chapter Two", CH2), False),
        ("OEBPS/images/cover.png", page_image("Cover", 2, size=(300, 450)), True),
    ]
    write_zip(out / "minimal.epub", entries)
    manifest["minimal.epub"] = {"kind": "epub3", "chapters": 2, "title": "Fixture Book",
                                "cover": "OEBPS/images/cover.png"}


def make_fb2(out, manifest):
    text = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">\n'
            '  <description>\n    <title-info>\n      <genre>prose</genre>\n'
            '      <author><first-name>Test</first-name><last-name>Author</last-name></author>\n'
            '      <book-title>Fixture FB2</book-title>\n      <lang>en</lang>\n    </title-info>\n'
            '    <document-info><author><nickname>noctopage</nickname></author>'
            '<date>2026-01-01</date><id>noctopage-fixture-fb2</id><version>1.0</version>'
            '</document-info>\n  </description>\n  <body>\n'
            '    <section><title><p>First Section</p></title>\n'
            '      <p>The first paragraph of a minimal FB2 book used by the Noctopage tests.</p>\n'
            '      <p>A second paragraph with <emphasis>emphasis</emphasis> and <strong>strong</strong> text.</p>\n'
            '    </section>\n'
            '    <section><title><p>Second Section</p></title>\n'
            '      <p>More text, including a little Japanese: \u3053\u3093\u306b\u3061\u306f\u3002</p>\n'
            '    </section>\n  </body>\n</FictionBook>\n')
    (out / "minimal.fb2").write_bytes(text.encode("utf-8"))
    manifest["minimal.fb2"] = {"kind": "fb2", "title": "Fixture FB2"}


def make_txt(out, manifest):
    lines = ["Fixture text file", "=================", "",
             "This plain UTF-8 file exercises the TXT path of the book reader.", "",
             "Second paragraph. Accented: caf\u00e9, na\u00efve, \u00fcber. "
             "Japanese: \u5409\u91ce\u5bb6\u306e\u725b\u4e3c\u3002", "",
             "The end."]
    (out / "minimal.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    manifest["minimal.txt"] = {"kind": "txt"}


def make_pdf(out, manifest):
    """Two-page PDF 1.4 written by hand; xref offsets are measured, not guessed."""
    def stream(text):
        return ("BT /F1 24 Tf 72 700 Td (%s) Tj ET" % text).encode("ascii")

    objs = {}
    objs[1] = b"<< /Type /Catalog /Pages 2 0 R >>"
    objs[2] = b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"
    for pg, (obj, cont) in enumerate(((3, 5), (4, 6)), start=1):
        objs[obj] = (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                     b"/Resources << /Font << /F1 7 0 R >> >> /Contents %d 0 R >>" % cont)
        s = stream("Noctopage fixture PDF - page %d of 2" % pg)
        objs[cont] = b"<< /Length %d >>\nstream\n%s\nendstream" % (len(s), s)
    objs[7] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"

    out_bytes = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}
    for n in sorted(objs):
        offsets[n] = len(out_bytes)
        out_bytes += b"%d 0 obj\n%s\nendobj\n" % (n, objs[n])
    xref_pos = len(out_bytes)
    count = max(objs) + 1
    out_bytes += b"xref\n0 %d\n" % count
    out_bytes += b"0000000000 65535 f \n"
    for n in range(1, count):
        out_bytes += b"%010d 00000 n \n" % offsets[n]
    out_bytes += (b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n"
                  % (count, xref_pos))
    (out / "two_pages.pdf").write_bytes(bytes(out_bytes))
    manifest["two_pages.pdf"] = {"kind": "pdf", "pages": 2}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    manifest = {}
    pages = make_cbz_natural(out, manifest)
    make_cbz_mixed(out, manifest)
    make_cbt(out, manifest, pages)
    wrote_cb7 = make_cb7(out, manifest)
    make_folder_comic(out, manifest)
    make_tall(out, manifest)
    wrote_webp = make_webp(out, manifest)
    make_japanese_name(out, manifest)
    make_epub(out, manifest)
    make_fb2(out, manifest)
    make_txt(out, manifest)
    make_pdf(out, manifest)

    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                                       encoding="utf-8", newline="\n")

    total = 0
    for p in sorted(out.rglob("*")):
        if p.is_file():
            total += p.stat().st_size
            print("%8d  %s" % (p.stat().st_size, p.relative_to(out).as_posix()))
    print("total %d bytes" % total)
    if not wrote_cb7:
        print("SKIPPED: natural_order.cb7 (no py7zr, no 7z/7za/7zr on PATH)")
    if not wrote_webp:
        print("SKIPPED: page.webp (Pillow lacks WebP)")
    print("SKIPPED: CBR (rar cannot be written without the proprietary rar tool)")
    if total > 2 * 1024 * 1024:
        print("ERROR: fixtures exceed 2 MB")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
