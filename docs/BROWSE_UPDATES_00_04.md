# NoctoPage 00.04

Install `Noctopage-00.04.vpk` once with VitaShell to add the new browser and updater.
The app keeps title ID `NOCT00001`, your library and your reading state.

## Browse

Open **Discover** in the sidebar. Choose a source to load a shelf-style cover grid.
Use the D-pad and Confirm, or tap a cover; drag to scroll. A comic's detail page shows
its cover, author, description and available formats. Tap **Download** or use Confirm.
Downloads appear under **Transfers** and completed files are scanned into your shelf.

Four built-in sources are supplied:

- Internet Archive — comics explicitly marked with its public-domain mark licence.
- Wikimedia Commons — comic-related, freely licensed PDF files; details include the
  licence reported by Commons. Search results may include comic-related documents.
- Project Gutenberg — public-domain books through OPDS.
- Open Library — its OPDS catalogue. Borrow-only or paid links are not downloaded.

The two new adapters use the providers' public JSON APIs. Cover images load in the
background. Internet Archive restrictions and private files are respected.

Tap the visible **Search comics…** control or press Square. Archive and Commons search
the service; OPDS sources use their search link when available. Otherwise search filters
titles/authors on the current page. Clear the text to show that page again. Previous/Next
cards load additional pages. Triangle reloads the feed and retries failed cover requests.

**Add custom catalogue** accepts an HTTP(S) OPDS URL (Komga, Kavita, Calibre-Web, etc.)
or an `https://archive.org/details/COLLECTION_ID` collection link. Ordinary HTML comic
websites need their own adapter and are not accepted as an OPDS feed. Custom Archive
links represent collections, rather than individual item pages. You can edit/remove a
custom source with Triangle. Optional HTTP Basic credentials stay with the source's
origin; they are not forwarded to unrelated image or download hosts.

## GitHub auto update

Settings includes **GitHub update repository**, **Check for updates / install**, and
**Check for updates at startup**. The official default is **stevenjc2009-byte/noctopage**.
No setup or token is needed. You can enter another public GitHub URL or `owner/repository`
if you intentionally want to use a custom release source.
Startup checks are enabled by default once a source is configured. An available update
shows a short notification. Installation requires choosing Download, then Install & restart
after verification; it is performed by the Vita's package promoter inside the app.

The official source and releases are at https://github.com/stevenjc2009-byte/noctopage.
Absent or empty repository settings use this default; previously saved custom sources remain
in effect. Private repositories requiring a token are not supported.

The publisher must provide a stable GitHub Release with:

- A tag matching the Vita version, such as `v00.05` or `00.05`.
- `Noctopage.vpk`, or exactly one `Noctopage-*.vpk` asset. When both exist the canonical
  `Noctopage.vpk` wins; ambiguous variants are rejected.
- GitHub's `sha256:` asset digest. Legacy assets without a digest must be re-uploaded.
- `TITLE_ID=NOCT00001` and a matching `APP_VER` in `sce_sys/param.sfo`.
- `eboot.bin` and `sce_sys/package/head.bin`. Version 00.04 includes the install header
  in its VPK; future releases should keep it. The runtime version follows Makefile's APP_VER.

The app checks the release URL, size and digest before installation, rejects unsafe ZIP
paths, duplicate filenames, symlinks and incomplete packages, and checks free space.
It downloads/extracts on a worker. Back cancels a download. An install failure is saved
and shown on the next launch. The app closes readers, workers and the database before
promotion; the result is checked before reporting success. Restart uses the Vita loader;
if the loader fails, reopen NoctoPage from LiveArea. Comics, database and manual backups
remain outside the installed app directory. No automatic library backup was added.

## Validation and limits

The VitaSDK build succeeds with warnings treated as errors. Sanitized host tests cover
the release parser, real captured Archive/Commons responses, format selection and valid
and malformed update archives. The actual built VPK also passes the updater's own
SHA-256, SFO identity/version, ZIP validation and complete extraction path on the host.
All four live catalogue URLs returned valid responses. The Archive sample offered PDF
and EPUB downloads; Commons returned 24 PDFs.

The published release API, asset SHA-256 and download are checked from the host.
Physical-Vita installation/restart remains unverified. Publisher licensing/attribution is in `updater-attribution.txt`
and `GPL-3.0-VitaShell.txt`.

## Bug fixes

- Search requests are cancelled when leaving/replacing a page, so stale results cannot reopen it.
- Removing a custom source clamps the selected row and scroll position.
- Reload retries failed covers; leaving the browser cancels unused cover requests and menus.
- Credentials are scoped to the source origin and cached covers are scoped to its credentials.
- Download menus reserve space for Read now, filter unsupported/paid/borrow links, and preserve
  existing ZIP/MOBI compatibility. Comic API sources prefer CBZ/CBR/PDF over EPUB.
- Local catalogue filtering uses the existing Unicode-aware search function.
- The queue preserves URLs up to 2,047 bytes through persistence and worker downloads;
  a 1,000-character URL was downloaded successfully in a regression test.
- GitHub repository names match case-insensitively, as GitHub itself accepts them.
- Updater version detection follows APP_VER so later builds do not report an older compiled version.
