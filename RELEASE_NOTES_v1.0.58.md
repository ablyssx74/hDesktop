# hDesktop v1.0.58

Haiku's popups get the pill look of the Snake Tracker desktop: the title overlays, the date and CPU labels and the dock's menus are now rows of rounded pills, and only one popup is open at a time.

## Highlights

**Haiku**
- **Title overlays are pills.** Each window title in the hover list is its own pill with fully rounded ends, in the same light tint of the **Selector Color** as Snake Tracker's selected labels (very light on a dark theme, a little deeper on a light one), with dark text. The title under the pointer turns the Selector Color itself. The pills' corners show the wallpaper (or whatever is behind them) instead of a square.
- **The date and CPU hover labels are pills too.** The date above the clock and the "CPU: N%" label keep their colors and fonts; only the shape changed.
- **The dock's menus match.** The leaf's Preferences menu, the trash menu, the tray icons' menus and the app drawer's Add/Remove Favorite menu are pills, and so are the three dropdowns in Settings (Dock Location, Open Effects, Close Effects, where a small dot marks the current choice). A choice that is not available shows as a muted pill. The menus do the same as before; only the look changed.
- **Popups open clear of the icon.** The icons are still growing from their hover zoom when a popup opens, so a popup over an icon showed a piece of it in its corners. The title list now clears the icon at its full zoom size, and the leaf, trash and tray menus open above the icon on a bottom dock and below it on a top dock.
- **One popup at a time.** No hover popup (title list, thumbnail, workspace preview) opens while a menu is open, and opening a menu closes the ones that are showing, so two popups can no longer blend into each other while they settle. Opening a menu closes any other pill menu first, and the Tracker folder menu and the CPU menu close an open pill menu as they start.

## Not changed
- The **Tracker folder menu** (right-click on the Tracker icon) and the **CPU graph menu** are still stock menus: the first is Tracker's own folder browser with submenus, the second has live submenus (running apps, memory, threads).

## Linux
- No changes in this release; the version number is the same as Haiku's, as before.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM: the title list with one and with several windows (look, hover color, corners over the wallpaper and over other windows), the date and CPU labels, and the pill menus' look (marked, muted and hovered choices, separators) in a test program. The leaf, trash, tray, drawer and Settings menus were not opened with a real click there, so their actions and positions are still to be checked on real hardware.
