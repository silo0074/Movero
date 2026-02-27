* Sun Feb 27 2026 Liviu Istrate <github.com@silo0074> - 1.0.2
### Fixed
- Accessing clipboard on Wayland fix is done using a dummy window that previously was hidden 
after Settings window was shown which lead to app remaining open.

* Sun Feb 25 2026 Liviu Istrate <github.com@silo0074> - 1.0.1
### Fixed
- Fixed the issue on Arch based distributions where the clipboard data could not be accessed to read copied files.

* Sun Feb 08 2026 Liviu Istrate <github.com@silo0074> - 1.0.0
### Added
- Initial release of **Movero**.
- Support for **xxHash** data integrity verification.
- Real-time speed/time performance graph using Qt.
- **D-Bus** integration for file manager highlighting.
- KDE Service Menu for "Paste with Movero" integration.