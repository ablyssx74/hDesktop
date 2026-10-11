# hDesktop v1.0.59

On Haiku the dock's Tracker menu now joins up like the Snake Tracker menus: each level overlaps the one before it, and the lit trail is shaded like a raised round bar.

## Highlights

**Haiku**
- **Menus overlap their parent.** Each submenu of the dock's Tracker folder menu opens a little over its parent, which covers the parent's arrow and keeps the trail from one menu to the next unbroken. The corner of a submenu that touches its parent is square, so no wallpaper shows there.
- **The trail is a raised bar.** The lit row and the trail that runs from menu to menu are rounded and shaded like a round bar lit from the top left: lighter on top, darker underneath, with soft ends. It follows the **Selector Color** and light and dark themes.
- **Long folder lists stay joined.** A list taller than the screen is clipped to it and still joins its parent's row.

## Linux
- No changes in this release; the version number is the same as Haiku's, as before.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM (64-bit): the dock's Tracker menu with several levels of folders, long lists, and the trail's shading. The 32-bit package was built but not run.
