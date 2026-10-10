# Changelog

## 00.04

- Official GitHub repository and stable-release downloads, with this repository configured
  as the updater's default. Empty settings use the default; custom sources remain supported.
- Settings update checks, startup checks, asynchronous VPK download and verification,
  Vita package installation and restart path.
- Cover-grid catalogue browsing, touch scrolling, visible search, descriptions and downloads.
- Internet Archive public-domain comic and Wikimedia Commons PDF adapters; custom OPDS
  and Archive collection links.
- Fixes for stale search requests, source-list scrolling, credential scope, cover retries,
  download format selection/menu bounds and long encoded download URLs.
- GitHub repository name case handling, version checks, SHA-256 verification and safe
  package validation/extraction, with host regression coverage.

Physical-device package promotion and restart remain unverified in the development environment.

## Earlier development

00.03 added PIN-protected Private, bulk actions and undo, collections, independent series
covers, series reader settings, aggregate progress, completion badges, missing-volume
footers and manual reading-state-only backups. Earlier phases introduced the library,
comic/PDF and ebook readers, OPDS browsing, transfers, reading history and settings.
See the source history and `docs/` for development details.
