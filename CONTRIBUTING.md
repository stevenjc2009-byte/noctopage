# Contributing

Please use an issue to report a reproducible bug or discuss a substantial feature.
Include the NoctoPage version, Vita model/firmware, file format, steps and a relevant log
excerpt. The device log is `ux0:data/noctopage/log.txt`; remove private information before posting.

Use C (`gnu11`) and preserve `-Wall -Wextra -Werror`. Pure code belongs in the appropriate
`src/core`, `src/lib`, `src/net`, `src/comic`, `src/doc` or `src/reader` module, with meaningful
host regression tests when behaviour changes. Vita UI/platform code should remain thin.
Only the main thread may create/free GPU textures; network and decoding work belongs on workers.
See `docs/PLAN.md` for architecture and build conventions.

Run the affected tests and Vita build. Host tests use AddressSanitizer and
UndefinedBehaviorSanitizer. Explain physical-device verification separately: a successful
host test or build does not demonstrate Vita UI/installation behaviour.

Do not commit comics, user databases, credentials, generated build trees or VPKs.
VPKs belong in GitHub Releases. Keep source and third-party licence/attribution notices.
For releases, bump `APP_VER`, commit the matching source, validate the VPK, and attach it
to a stable `vMM.mm` release as described in the README.
