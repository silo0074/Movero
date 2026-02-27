# Changelog

All notable changes to this project will be documented in this file.

## [1.0.2] - 2026-02-27
### Fixed
* Accessing clipboard on Wayland fix is done using a dummy window that previously was hidden after Settings window was shown which lead to app remaining open.

## [1.0.1] - 2026-02-25
### Fixed
* Fixed the issue on Arch based distributions where the clipboard data could not be accessed to read copied files.

## [1.0.0] - 2026-02-08
### Added
- Initial release of **Movero**.
- Support for **xxHash** data integrity verification.
- Real-time speed/time performance graph using Qt.
- **D-Bus** integration for file manager highlighting.
- KDE Service Menu for "Paste with Movero" integration.
