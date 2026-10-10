# NoctoPage — series, Wi-Fi and library backups

## Continue a series

Keep each series in its own folder, for example `ux0:books/Tokyo Ghoul/Volume 01.cbz`.
On Your shelf, highlight a series and press **Select** to continue it immediately. **Triangle → Continue series** does the same thing. **Confirm** still opens its volume list.
Inside a series, Select continues that series. On a standalone book tile, Select uses the global Continue Reading shortcut.

Continue Series resumes the most recently started unfinished volume. If none is started, it opens the first unread volume in natural filename order (`Volume 2` before `Volume 10`). Finished and missing volumes are excluded. If everything available is finished, the app says so. Edited display titles do not change volume order.

## Upload books over Wi-Fi

1. Connect the Vita and your phone or PC to the same Wi-Fi.
2. Open **Settings → Wi-Fi book uploads**.
3. In your phone/PC browser, open the address shown by the Vita, such as `http://192.168.1.20:8080`.
4. Enter the six-digit pairing code shown on the Vita.
5. Optionally enter a series folder, select one or more supported book files and upload them.

Files stream sequentially in 64 KiB chunks rather than occupying RAM as complete books. Books are placed only below `ux0:books`. Series folders can be nested up to eight levels, with no mount names, hidden components or traversal paths. Uploads reject an existing filename; rename the file on your computer if you want to keep both. The maximum individual upload is 2 GiB. Interrupted uploads remove their temporary file. Successful uploads request a library rescan, including when another scan is already underway.

Keep the upload screen open. **Back** stops the server and cancels an active transfer. Automatic standby is held off while this screen is open. The server uses local HTTP and a new code each time it opens. It is available only while explicitly enabled in the app. Some Wi-Fi networks isolate clients; on those networks use VitaShell instead.

You can also upload a NoctoPage `.npb` backup. These are routed to the app's backup directory rather than your books folder. Then close uploads and open Restore.

## Back up or restore your library

Open **Settings → Library backup & restore**.

**Create portable library backup** produces a timestamped `.npb` file in `ux0:data/noctopage/backups`. Copy it to your PC with VitaShell.

A backup contains book metadata, edited titles/authors, reading positions, finished/favourite flags, reading statistics, bookmarks, per-book reader settings and custom cover thumbnails. Local image cover overrides are normalised into portable thumbnails. Missing or undecodable custom cover images cannot be included. Original book covers can be regenerated from book files. The backup excludes book files, global app preferences, OPDS server credentials and the download queue.

**Restore a saved backup** lists up to 128 `.npb` files in the backup directory. Choose one and confirm Restore. The app first creates a `before-restore-...npb` safety backup of your current library. If that safety backup cannot be written, restore stops.

Restoration replaces saved reading metadata and per-book settings for the books present in the backup, while keeping the current Vita's file paths and any books absent from that backup. Book identity uses the existing content ID: a missing backed-up book reconnects when you copy the same file back later. Existing custom covers are never overwritten while importing; extracted covers receive unique names. Database changes commit together or roll back on an error. Cached covers refresh after restore.

Portable backup limits: 10,000 book records, 100,000 settings, 100,000 bookmarks, 64 MiB of SQLite metadata and 10,000 custom thumbnails. Imported archives accept only the expected metadata and cover entries, bounded by their decoded sizes. The job runs in a worker while scanner and cover updates are paused. Keep the app open until it finishes.

## Build status

Compiled and packaged with VitaSDK, with application warnings treated as errors. No tests, emulator sessions, hardware checks or speed benchmarks were run, as requested. Runtime behavior still needs confirmation on a Vita. The pre-existing SDK libarchive fallback lacks LZMA/CB7 support; CBZ uses ZIP and is unaffected by that codec limitation.
