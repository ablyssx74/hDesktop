# hDesktop v1.0.64

The app drawer is in the Snake Tracker look, on Haiku and on Linux.

## Highlights

**Haiku**
- **The drawer is a lavender panel with raised pills.** The panel is the Selector Color's light tint with rounded corners, and the title, the Power off and Restart system buttons, the Favorites and Applications tabs and the app under the pointer are raised pills lit from the top left. The button and title text have a dark shadow, with a red face for Power off, a blue one for Restart system and a dark-blue one for the title. The sections are divided by soft grooves, and the drawer follows Snake Tracker's preferences: the Selector Color, and the "Snake look" switch, with which off the drawer keeps its dark look.
- **The corners are drawn from a small capture.** The drawer has a fixed size and place, so only its four corners are read from the screen, once, before it is shown (not the whole desktop).

**Linux**
- **The same drawer**, with a sunken search field and a "Log out" pill as well.
- **A "Menu & Drawer Color" setting** (Settings, under Selector Color) for the panel behind the pill menus and the drawer. It follows the Selector Color's tint until it is set, so a dark panel is possible now (black as the Selector Color gave a grey panel with a black snake). Press Defaults in its picker to follow the Selector Color again.
- **A "Transparency" slider for the drawer's panel**, on the same line as the color (0 to 80 %). Only the panel fades; the pills, text and icons stay solid.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`. The new settings are saved in `hdesktop.conf` as `menu_color` and `drawer_transparency`; nothing changes until they are set.

## Tested on
- Haiku R1/beta6 in a VM and on real hardware (64-bit). Linux (KDE Plasma, Wayland): the drawer, the color and the slider. The 32-bit package was built but not run.
