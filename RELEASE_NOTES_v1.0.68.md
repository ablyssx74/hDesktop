# hDesktop v1.0.68

Fixes for the Tracker icon, a rounded workspace popup, and a CPU graph menu without chevrons.

## Highlights

**Haiku**
- **The Tracker icon works after Tracker's preferences were closed.** A closed preferences window is only hidden, and the dock counted it as a Tracker window, so the icon would not open Tracker and the menu's Preferences did nothing. Hidden windows are now left out.
- **The workspace popup has rounded corners** when Rounded Edges is on, and square ones when it is off.
- **The CPU graph's menus have no chevrons**, like ProcessController's.

**Linux**
- **The CPU graph's menus have no chevrons** either.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: rebuild and install as before.

## Tested on
- Haiku R1/beta6 in a VM (64-bit). The 32-bit package was built but not run. The Linux change was not run on a desktop before release.
