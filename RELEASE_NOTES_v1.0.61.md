# hDesktop v1.0.61

On Haiku the dock's Tracker menu opens without the delay that slow graphics hardware gave it.

## Highlights

**Haiku**
- **No lag when the Tracker folder menu opens.** The pill look needs the picture of the screen behind each menu (a window is a rectangle, and the round corners show what lies under it). That picture was taken of the whole screen when the menu opened, close to a second on some hardware. Each menu level now captures only the part of the screen it covers, just before it opens, so the cost is a small fraction and no longer depends on the screen's size. The menus look the same.

## Linux
- No changes in this release; the version number is the same as Haiku's, as before.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM (64-bit): the dock's Tracker menu with several levels, long lists, and over other windows. The slow-hardware timing itself was not measured; the 32-bit package was built but not run.
