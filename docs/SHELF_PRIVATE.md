# NoctoPage 00.01 — grouped series and Private

Install `Noctopage.vpk` with VitaShell to update NoctoPage. The title ID remains `NOCT00001`; existing reading positions, favourites, covers and bookmarks remain in the existing database.

## Grouping your shelf

Put each series in its own folder under `ux0:books`, for example:

```
ux0:books/Tokyo Ghoul/Volume 01.cbz
ux0:books/Tokyo Ghoul/Volume 02.cbz
ux0:books/Tokyo Ghoul/Volume 10.cbz
```

Your shelf shows a single **Tokyo Ghoul** card. Tap it or press Confirm to browse the volumes. Back returns to the series shelf. This works with any series name and supported book format, including CBZ, PDF and EPUB. Nested folders retain their own groups.

Loose files directly in `ux0:books` can also group automatically when their names have an explicit volume marker, such as `Tokyo Ghoul Volume 01.cbz`, `Tokyo Ghoul Vol. 02.pdf` or `Tokyo Ghoul v03.epub`. Use a consistent filename convention for volume ordering. Bare numbers such as `1984.epub` are not guessed as series names.

For filenames that cannot be recognised, open the volume's three-dot menu and choose **Set series**. Assign the same name to the volumes you want combined. Clearing the name restores automatic grouping. A manual series name takes priority over the folder or filename. In a long menu, swipe or tap the footer to see the last options.

Grouping is on by default. If you previously turned it off, press Square and choose **Toggle series / all books**. Search, filters and sorting are saved separately for the public and private shelves.

## Series covers

A series uses the first volume in natural filename order as its cover. Filtering the shelf no longer changes that representative cover. Open the series card's three-dot menu to choose a local series cover image or find one online. The chosen image is saved as the first volume's cover and reused for the series card. Online matching still depends on catalogue availability; check the preview before confirming.

## Moving volumes to Private

Open a series, then tap the three dots in a volume card's lower-right corner. Choose **Move to Private**. Triangle opens the same menu for the selected volume.

The new **Private** sidebar section contains those volumes and supports the same grouping and browsing. To return a volume, use **Move to Your shelf** in its menu.

You can also move an already-open comic, PDF or EPUB. Show the reader overlay with Triangle or a centre tap, then tap the three-dot button at the top right. The reader menu includes the same move action.

Private volumes stay out of the public shelf, its search and filters, Recent, and the public Continue shortcut. Continue Series and Next Volume stay within the current public/private section. Moving a book changes metadata, not its file path or saved reading position. The choice survives rescans, restarts and portable library backups.

Private is a separate shelf, **not a password lock or encryption**. Files remain accessible through VitaShell. Global reading statistics still aggregate the library. Restoring an older backup without private/series settings restores those books to their earlier public/automatic-grouping state; the existing restore flow makes a safety backup first.

## Other fixes

- Long menus stay within the screen and support touch scrolling.
- Streaming comics can open their reader menu correctly.
- An empty streaming cache no longer passes a null allocation to the sorting function.
- A scan that changes metadata without changing the number of books refreshes the shelf correctly.
- Successful favourite/read-state changes no longer produce false failure logs.
- Shelf queries load only the current public/private section, and Next Volume limits candidates to that section.

## Validation

VitaSDK built the final version 00.01 package with application warnings treated as errors. The host suite passed **44,403 checks with zero failures**, using AddressSanitizer and UndefinedBehaviorSanitizer with undefined behaviour treated as fatal. Package ZIP integrity, title ID and version metadata were verified. Host UI tests use the real grid and menu input code with a stub renderer; physical Vita touch and rendering still require a device check.
