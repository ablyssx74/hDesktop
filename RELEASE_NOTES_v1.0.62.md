# hDesktop v1.0.62

The dock's Tracker menu follows Snake Tracker's new preference, and big folders are quicker to browse.

## Highlights

**Haiku**
- **Follows "Snake look for Tracker and Deskbar".** With that Snake Tracker preference (Tracker preferences > Windows) off, the dock's Tracker menu is the plain rectangular menu, not the pill look. It is read each time a menu opens.
- **Quicker big folders.** The pill look's picture of what lies behind the menu and the pill itself is worked out once for each menu level; moving the pointer then puts only the lit row over it.

**Linux**
- **Quicker big folders.** The pill menu draws the lit row and its shading in a small box round them, not over the whole menu, so a folder with hundreds of entries no longer redraws every pixel as the pointer moves.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM (64-bit) and Linux (KDE Plasma on Wayland): menus several levels deep, long lists, the preference on and off. The speed-up on slow graphics hardware was not measured. The 32-bit package was built but not run.
