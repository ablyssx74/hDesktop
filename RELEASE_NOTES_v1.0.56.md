# hDesktop v1.0.56

The Tracker menu gets a sharper look on both platforms: a dark outline round the snake trail, a flat selector
(with a setting to bring the bevel back), and the selected row sticking out of the menu's outer edge as a rounded
tab. Haiku's window title overlays and hover highlights also follow the colors you chose.

## Highlights

**Linux and Haiku**
- The selector has a one pixel **dark outline** that follows the whole trail: the rows, the bar down a
  submenu's edge and the curves that join them. It reads well on dark and light themes alike.
- The selector is **flat** by default: one color inside the outline. A new setting brings back the light top
  edge and dark bottom edge.
- The **selected row sticks out of the menu** on its outer edge, as a small outlined tab, so the open path looks
  like one shape from the root menu to the last submenu. It flips sides when a submenu opens on the other side of
  the screen.

**Haiku**
- Window **title overlays** use Haiku's color scheme, and the title list and thumbnail hover highlights follow the
  **Selector Color**.

## Linux

### New
- **Settings > Flat Selector** (on by default) turns the flat fill on or off. Off gives the old bevel. The outline
  stays either way.
- **Dark outline and bulge** on every menu the dock draws, not just the Tracker menu.
  - The bulge is drawn in a 3 px transparent margin on each side of every popup menu. The menus themselves are
    unchanged in size and position: submenus are anchored so the menu bodies still meet, whichever side they open
    on.
  - The tab is rounded at its outer end, like the selector's other corners.

### Fixed
- No dark line where two menus join: the outline is only drawn in the margin on the tab's side.

## Haiku

### New
- **Settings > Flat** (on by default), beside Snake Trail, with the same meaning as on Linux. It is saved as
  `nav_snake_flat`, the setting snaketracker already reads, so hDesktop's Tracker menu and Tracker's own menus
  follow the same choice while hDesktop runs.
- **Dark outline and bulge** on the custom Tracker navigation menu. The tab is a small borderless window just
  outside the menu's outer edge, joined to the selected row. It is square-cornered (a window can't have see-through
  corners).

### Changed
- Title overlays use the Haiku color scheme (`4d88fda`).
- Title-list and thumbnail hover highlights follow the Selector Color (`5a646ea`).

## Upgrading

**Linux**
- `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install` from a checkout. No new dependencies.
- Existing settings are read as before; Flat Selector starts on.

**Haiku**
- Install the package for the whole system, or rebuild with `make release`. Flat starts on.

## Notes and limitations
- The Linux bulge depends on the compositor honoring a popup's transparent margin. It has been checked on KDE
  Plasma (KWin) only.

## Tested on
- KDE Plasma (KWin) on CachyOS: the menus were opened and the hover path driven by a test build, and checked by
  screenshot, including the join between menus. Real pointer hovering, scrolling menus and menus at screen edges
  were not exercised on Linux.
- Haiku R1/beta6 in a VM: the Tracker menu with outline, flat fill and bulge, and the Settings window.
- Other Wayland compositors (Sway, Hyprland, labwc, Wayfire, niri) were not tested for this release.
