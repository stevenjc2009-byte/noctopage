# NoctoPage

<img src="sce_sys/icon0.png" width="96" alt="NoctoPage icon">

A comic, manga and ebook reader for **PlayStation Vita**, with grouped series shelves,
reading progress, a PIN-protected Private section, online catalogue browsing and GitHub updates.

**[Download the latest VPK](https://github.com/stevenjc2009-byte/noctopage/releases/latest/download/Noctopage.vpk)** ·
[All releases](https://github.com/stevenjc2009-byte/noctopage/releases) ·
[Report a bug](https://github.com/stevenjc2009-byte/noctopage/issues)

## Install

1. Download **Noctopage.vpk** from the latest release and copy it to your Vita.
2. Install it with VitaShell on a homebrew-enabled Vita. The title ID is `NOCT00001`;
   install subsequent versions over the existing app to keep your reading state.
3. Put your comics and books in **`ux0:books/`**, including subfolders.
4. Open NoctoPage. If you add books while it is running, use **Settings → Look for new books now**.

VPKs are provided as release assets, rather than stored in the source repository.
Version **00.05** already points to **`stevenjc2009-byte/noctopage`** for updates.

## Features

- **Series shelves:** volumes are grouped into one series card. Open it to see each volume.
  Series covers can be chosen independently of volume covers.
- **Reading progress:** series bars account for all owned volumes; completed volumes have
  a full bar and a green **Complete** badge.
- **Comic reading:** right-to-left manga mode, page fitting, double-page spreads, white-border
  cropping, rotation, bookmarks and saved positions. Reader settings can apply to a series.
- **Private section:** PIN protection, idle locking and volume/bulk move actions, with undo.
  This locks the section in the app; it does not encrypt the files on your memory card.
- **Collections:** create your own lists, add volumes or series, and browse Reading next or Completed.
- **Missing-volume footer:** cached/manual series totals and optional online manga metadata lookup.
- **Discover:** touch-scrollable cover cards, descriptions, search, pagination and downloads.
  Includes Internet Archive public-domain comics, Wikimedia Commons PDFs, Gutenberg and Open Library.
- **Custom catalogues:** OPDS URLs for Komga, Kavita, Calibre-Web and similar servers,
  plus Internet Archive collection links. Ordinary HTML websites require an adapter.
- **Transfers and Wi-Fi import:** download supported free files or upload books to your Vita.
- **Manual reading-state backups:** small files containing positions and progress, without comic
  files or cover images. No automatic comic/library backups are created.

## File formats

| Type | Formats |
|---|---|
| Comic archives | CBZ/ZIP, CBR/RAR, CB7/7Z, CBT/TAR |
| Image-folder comics | Supported image files in a folder |
| Fixed-page documents | PDF, XPS/OXPS, SVG |
| Reflowable books | EPUB, MOBI, FB2, TXT, HTML/XHTML |

For comics, prefer **CBZ/CBR or PDF** when available; they preserve the original pages.
EPUB is also supported, particularly for text-focused books. Files must be DRM-free.
Format and rendering behaviour can vary with the file; see the issue tracker for reports.

## Controls and data

Use **L/R** to change sections, the **D-pad** to select, and your system's Confirm/Back
buttons to open or return. Cover grids support tapping and drag scrolling. **Triangle**
opens menus and **Square** opens search/actions where shown in the hint bar.

- **Discover:** choose a source, tap Search or press Square, open a cover, then Download.
- **Volume actions:** use the three-dot menu or Triangle to move to Private or add to a collection.
- **Backups:** Settings → Reading-state backup & restore.
- **App data:** `ux0:data/noctopage/`; books/downloads: `ux0:books/`.
- **Log for bug reports:** `ux0:data/noctopage/log.txt`.

Detailed guides: [library and progress](docs/LIBRARY_PLUS.md),
[shelf and Private](docs/SHELF_PRIVATE.md), and [browsing and updates](docs/BROWSE_UPDATES_00_04.md).

## Updates from GitHub

**Settings → Check for updates / install** reads the latest stable release from this repository.
Automatic startup checks are enabled by default. No GitHub account or token is needed on the Vita.

When a newer version is available, choose **Download update**, then **Install & restart**
after verification. Downloads run in the background; Back cancels. The updater verifies the
GitHub asset's SHA-256 digest, app identity and version, and validates/extracts the VPK before
using the Vita package installer. Your books and reading state remain outside the app directory.

**Settings → GitHub update repository** shows the official default and permits a custom public
repository. Previously saved custom settings are preserved. Only strictly newer versions install;
the 00.05 app correctly reports up to date when the latest release is also 00.05.

If 00.04 reports a content-decoding/decompression error when checking, install 00.05
manually with VitaShell once. This version requests uncompressed HTTP responses.

The download/validation path and live GitHub release metadata are tested. **Installation and
restart on a physical Vita have not been verified in this release's development session.**
If restart fails, reopen the app from LiveArea. Please report device failures with the log.

## Build from source

The project uses C (`gnu11`), VitaSDK, vita2d, MuPDF, libarchive, SQLite and curl/mbedTLS.
Build on Linux or in WSL with VitaSDK installed and its required ports available.

```sh
git clone https://github.com/stevenjc2009-byte/noctopage.git
cd noctopage
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
bash tools/build_mupdf.sh vita
bash tools/build_libarchive.sh
bash tools/build_vita.sh
```

The output is **`build/Noctopage.vpk`**. The dependency scripts pin their source downloads;
curl/mbedTLS headers and static libraries are already vendored. See
[MuPDF notes](third_party/mupdf/README.md), [libarchive notes](third_party/libarchive/README.md)
and [TLS provenance](third_party/README-vita-radio-tls.md).

For host tests, install GCC/make, Python 3 and the development packages for libarchive,
liblzma, libpng, libjpeg-turbo, libwebp, curl, OpenSSL, FreeType and HarfBuzz.
Build the host MuPDF libraries, then run the sanitizer-enabled suite:

```sh
bash tools/build_mupdf.sh host
UBSAN_OPTIONS=halt_on_error=1 make -C tests HOSTDEPS=/ -j4 run
```

The default host dependency prefix is `$HOME/noct-hostdeps`; `HOSTDEPS=/` uses Ubuntu's
system development packages. Some test linker paths are specific to x86-64 Ubuntu/WSL.
Host tests cover parsing, archive safety, reading state, library grouping, downloads and updater
validation; they do not replace physical-device UI/installation testing.

## Publishing a future release

1. Increment **`APP_VER`** in `Makefile` (Vita format `MM.mm`, e.g. `00.05`).
   The compiled updater version follows this value.
2. Build, run the relevant tests, and validate with `python3 tools/check_vpk.py build/Noctopage.vpk`.
3. Commit the source and publish a stable GitHub Release tagged **`v00.05`**, matching the VPK.
4. Attach **`Noctopage.vpk`**. GitHub supplies the SHA-256 digest consumed by the updater.
   A versioned copy and checksum file are convenient optional assets.

Keep `TITLE_ID=NOCT00001` and `sce_sys/package/head.bin`. Draft/prerelease assets, missing
digests, mismatched identities/versions and ambiguous VPK variants are rejected. Release uploads
must come from the matching source commit so the bundled app and source stay aligned.

## Licence and contributions

NoctoPage is licensed under **[AGPL-3.0](LICENSE)**. Dependencies keep their respective licences;
see the `third_party/` notes and [VitaShell updater attribution](docs/updater-attribution.txt).
No commercial comics are bundled. Catalogue files and their licensing remain with their providers.

Issues and pull requests are welcome. Include your Vita model/firmware, NoctoPage version,
reproduction steps and a relevant log excerpt. Review logs before posting private titles or server
addresses. Source changes should retain the strict warnings and include relevant host regression tests.
