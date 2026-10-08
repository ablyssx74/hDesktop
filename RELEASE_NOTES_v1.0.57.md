# hDesktop v1.0.57

The Tracker menu's trail gets the same treatment on the vertical part that the selected row got in v1.0.56.

## Highlights
- **The vertical part of the snake trail hangs into the parent menu.** Where a submenu's selected row is not level with the row that opened it, the strip joining the two now hangs a few pixels into the parent menu, outlined on its inner side and rounded at its free end, so the whole trail reads as one shape. Where the submenu's row is lower (or higher) than the parent menu reaches, the strip carries on past the parent's edge. When the rows are level nothing changes.

## Linux
- Drawn in the transparent margin each menu already has on both sides (added in v1.0.56 for the row's bulge), so the free end is rounded even over the desktop.

## Haiku
- Drawn as a small window over the parent menu's edge, like the row's bulge. Its free end is rounded where it hangs over the parent's body and square where it runs on over the desktop (a window can't be see-through).

## Upgrading
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`. Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.

## Tested on
- Haiku R1/beta6 in a VM: the Tracker menu with the strip below and above the parent's row, past the parent's extent and over its body.
- KDE Plasma (KWin) on CachyOS: the menus opened and the hover path driven by a test build, with the submenu's row far below the parent's. Real pointer movement, menus at screen edges and other compositors were not exercised.
