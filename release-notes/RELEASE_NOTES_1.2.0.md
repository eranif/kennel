# Release Notes - Version 1.2.0

**Release Date:** August 4, 2026

## Overview
Version 1.2.0 introduces significant improvements to session management, terminal functionality, theming capabilities, and application settings. This release focuses on enhancing user productivity through better organization, customization, and update mechanisms.

---

## New Features

### Session and Group Management
- **Session Groups**: Organize multiple terminal sessions into collapsible groups for better workspace management
- **Rename Sessions**: Easily rename individual terminal sessions to identify them at a glance
- **Move Sessions Between Groups**: Reorganize sessions by moving them between different groups
- **Session Closing**: Properly close sessions with improved cleanup
- **Persistent Session Icons**: Each session group is assigned a unique persistent colored icon for visual identification
- **Group Filtering**: Filter and search terminal groups in the main view

### Terminal Improvements
- **Scrollback Lines Setting**: Configure terminal scrollback buffer size in Preferences
- **Middle-Click Tab Closing**: Close terminal tabs with a middle mouse click for faster workflow
- **Alt+Arrow Navigation**: Use Alt+Left/Right arrow keys to navigate between session groups and selections
- **Terminal Link Handling**: Improved file path handling in terminal links for better productivity

### Themes and Appearance
- **Extensive Color Theme Collection**: Ships with multiple built-in color themes to suit various preferences
- **Real-Time Theme Updates**: Switch themes and fonts in the Settings dialog with live preview
- **macOS Bundle Resources**: Proper theme file support in macOS application bundles
- **Enhanced Notebook Styling**: Improved tab padding and visual appearance of session group tabs

### Settings and Preferences
- **Persistent Preferences**: Settings are now properly applied across session startup and navigation
- **Update Checking**: New automatic and on-demand update checking functionality
- **Toggle Startup Checks**: Enable or disable update checks at application startup
- **Enhanced Settings Dialog**: Replaced directory picker with text input and browse button for better UX
- **Timer-Based Job Design**: Improved background job scheduling with support for async execution and local time handling

### Platform Improvements
- **macOS Support**: Added theme files to macOS bundle resources
- **Build Configuration**: Simplified wxWidgets build configuration for static monolithic linking
- **Updated Build Process**: Enhanced wxWidgets build process using configure script

---

## Improvements

### Code Quality and Refactoring
- Refactored session title management and group handling
- Introduced `SessionGroupEvent` for better event-driven architecture
- Improved UI logic for group renaming and session management
- Enhanced MainView UI with better session creation logic
- Implemented custom flat tab art for session groups using DataViewListCtrl

### Dependencies
- Updated wxTerminalEmulator to latest version
- Updated wxTerminalEmulator dependency version

### Bug Fixes
- Fixed session title update logic
- Fixed indentation in UpdateChecker logging
- Improved logging and error handling in update checks

---

## Technical Changes

### Session Management Architecture
- Replaced legacy NotebookNavigationDlg with modern DataViewListCtrl-based UI
- Introduced SessionGroup component for centralized group management
- Enhanced MainView with improved session creation and management logic

### Theme Engine
- Support for real-time theme and font switching
- Organized color theme assets for better maintainability
- Proper theme file handling for all platforms

### Build System
- Static monolithic wxWidgets linking for simpler deployment
- Improved macOS bundle structure with proper resource handling
- Windows installer version updated

---

## Known Issues
None reported at this time.

---

## Migration Guide
No breaking changes in this release. All settings from version 1.1.0 are compatible with 1.2.0.

---

## Acknowledgments
This release represents significant effort in improving the session management architecture and user interface. Thank you to all contributors and testers who provided feedback.

---

## Installation
Download the latest installer from the official repository.

**System Requirements:**
- Windows 10 or later (Windows)
- macOS 10.13 or later (macOS)
- Linux with wxWidgets support (Linux)

---

For more information, visit the project repository or documentation.
