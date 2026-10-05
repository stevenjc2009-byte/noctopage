/* Generated-fixture data for test_mdoc.c (one translation unit includes it). */
#ifndef NOCTO_TEST_MDOC_DATA_H
#define NOCTO_TEST_MDOC_DATA_H

/* Only what MuPDF's EPUB parser reads (epub-doc.c): no namespaces or media types needed. */
static const char *const EPUB_FILES[4][2] = {
    {"mimetype", "application/epub+zip"},
    {"META-INF/container.xml", "<container><rootfiles><rootfile full-path=\"c.opf\"/>"
     "</rootfiles></container>"},
    {"c.opf", "<package version=\"2.0\"><manifest><item id=\"n\" href=\"t.ncx\"/><item id=\"c1\" "
     "href=\"c1.xhtml\"/><item id=\"c2\" href=\"c2.xhtml\"/><item id=\"c3\" href=\"c3.xhtml\"/>"
     "</manifest><spine toc=\"n\"><itemref idref=\"c1\"/><itemref idref=\"c2\"/><itemref "
     "idref=\"c3\"/></spine></package>"},
    {"t.ncx", "<ncx><navMap><navPoint><navLabel><text>Chapter 1</text></navLabel><content src=\""
     "c1.xhtml\"/></navPoint><navPoint><navLabel><text>Chapter 2</text></navLabel><content src="
     "\"c2.xhtml\"/><navPoint><navLabel><text>Part 2b</text></navLabel><content src=\"c2.xhtml"
     "#p20\"/></navPoint></navPoint><navPoint><navLabel><text>Chapter 3</text></navLabel>"
     "<content src=\"c3.xhtml\"/></navPoint></navMap></ncx>"},
};

/* One page: a 2x2 red inline raster image at (50,50)-(150,150) (top-left origin) and a red
 * vector square at (200,50)-(300,150). No xref on purpose: MuPDF's repair pass rebuilds it. */
static const char IMAGE_PDF[] =
    "%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages "
    "/Kids [3 0 R] /Count 1 >>\nendobj\n3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 "
    "792] /Contents 4 0 R >>\nendobj\n4 0 obj\n<< /Length 125 >>\nstream\nq 100 0 0 100 50 642 cm "
    "BI /W 2 /H 2 /CS /RGB /BPC 8 /F /AHx ID FF0000FF0000FF0000FF0000> EI Q 1 0 0 rg 200 642 100 "
    "100 re f\nendstream\nendobj\ntrailer\n<< /Root 1 0 R >>\n%%EOF\n";

/* FB2 whose coverpage is a 32x32 black PNG and whose one section starts with "T": page 0
 * must be the cover and page 1 the section (MuPDF once put a blank page before each). */
static const char COVER_FB2[] =
    "<FictionBook xmlns:l=\"l\"><description><title-info><coverpage><image l:href=\"#c\"/>"
    "</coverpage></title-info></description><body><section><title><p>T</p></title><p>x</p>"
    "</section></body><binary id=\"c\" content-type=\"image/png\">iVBORw0KGgoAAAANSUhEUgAAACAAAAAg"
    "CAAAAABWESUoAAAAEklEQVR42mNgGAWjYBSMAuwAAAQgAAF8we+4AAAAAElFTkSuQmCC</binary></FictionBook>";

#endif
