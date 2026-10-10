# NoctoPage 00.02 code review

This update fixes five issues found in the current shelf, navigation, input and database code.

1. **Mixed volume filenames could give the wrong order.** `Vol 10` could sort before `Volume 2`, affecting the first-volume cover, Continue Series and Next Volume. Explicit V/Vol/Volume markers now sort by their numeric value, with leading zeroes and large numbers handled without integer overflow. Unnumbered extras follow numbered volumes; other filenames retain natural ordering.
2. **Library search missed assigned series names.** Search now matches the manual series field as well as title, author and path.
3. **Accidental spaces could split a series.** Assigning ` Tokyo Ghoul ` now stores `Tokyo Ghoul`. An all-whitespace name restores automatic grouping.
4. **Sideways gestures could activate cards or menu actions.** Horizontal movement beyond the tap threshold now cancels the tap, just as vertical movement does. Vertical scrolling and ordinary taps remain available.
5. **Database deletion could partially remove saved data.** Removing a book's row, settings and bookmarks now runs under one mutex and SQLite savepoint. If a step fails, the database changes roll back together and the original error remains available. This transaction covers database records; the existing file-deletion flow is separate.

The changes retain the title ID `NOCT00001`, existing saved positions and the grouped/public/private shelf features. Install the version 00.02 VPK with VitaShell to update the app.

Targeted tests cover mixed-marker ordering, ordering transitivity, manual-series search, whitespace normalisation, horizontal swipe cancellation, and an injected database-deletion failure. The full host suite passed **44,483 checks with zero failures**, using AddressSanitizer and UndefinedBehaviorSanitizer with undefined behaviour treated as fatal. VitaSDK compiled version 00.02 with application warnings treated as errors. VPK archive integrity, version and title ID were verified. Host input tests use the real grid/menu code with a stub renderer; physical Vita input and rendering remain unverified.
