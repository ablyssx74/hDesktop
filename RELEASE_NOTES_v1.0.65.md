# hDesktop v1.0.65

A rebuilt settings window on Haiku, with a colour of its own for the drawer, and the drawer's scrolling fixed.

## Highlights

**Haiku**
- **Drawer Color.** The drawer's panel has its own colour now, like the Linux build's Menu & Drawer Color. Until it is chosen it is a light tint of the Selector Color; "Default" in its colour window goes back to that.
- **Use System Colors, for the drawer and for the Selector.** With the drawer's switch on, the drawer takes Haiku's menu background and menu selection colours from the Appearance preferences; with the Selector's on, the dock's menus do. The colour button beside each switch is greyed while it is on.
- **The two colour boxes are larger and follow the colour as it is chosen.** Each shows the colour in use, and changes as you move through its colour control, when you press Default, and when you tick Use System Colors. The drawer's follows the Selector Color until it is chosen.
- **Compact sliders.** The label is centred over a slim track, with the limit labels (Fast and Slow, Small and Large) at the two ends of the track on the same line, which takes a little over half the height. That made room for the new colour rows, so the window is no taller.
- **[?] tool tips** beside Flat and the two Use System Colors switches explain them.
- **The drawer no longer scrolls past its last row.** Haiku's scroll view gives a plain view's scroll bar a fixed range of 0 to 1000, so the drawer scrolled on into empty space when it held few apps, and could stop short of the last row with many. The range now follows the grid's real height, in both the Snake look and the dark one.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. The new settings (`nav_use_system_colors`, `drawer_use_system_colors`, `drawer_color_auto`, `drawer_color`) change nothing until they are set.
- Linux: no changes in this release.

## Tested on
- Haiku R1/beta6 in a VM and on real hardware (64-bit). The 32-bit package was built but not run.
