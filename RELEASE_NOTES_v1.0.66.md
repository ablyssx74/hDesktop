# hDesktop v1.0.66

The dock's menus match the Deskbar's, and a Rounded Edges switch for the dock's popups and tiles (Haiku).

## Highlights

**Haiku**
- **Menus in one panel, like the Deskbar's.** The menus the dock opens itself (a tray icon's right-click menu, the leaf, trash and icon context menus, and the dropdowns) were separate pills with gaps between them. They are one panel now, in the Selector Color's tint, with the row under the pointer a raised pill in the Selector Color and the separators soft grooves. A dropdown marks its current choice with a dot in the left margin.
- **Rounded Edges.** A new switch in the settings window (on by default, as before). Off, the corners of the tray and other menus, the Tracker folder menu and the title overlays are square, and so are the CPU graph, the CPU tooltip, the workspace switcher and the date and title capsules. The lit rows and the snake trail stay rounded, as in Snake Tracker. With it off a popup no longer takes a picture of the screen behind it, so it opens a little quicker. The title overlay has no dark corners in that mode: its row fills the window.
- **A tidier settings window.** Flat moved to the second column, Rounded Edges has a line of its own, and the window is a little taller to make room.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. The new setting (`nav_rounded_edges`) is on until it is changed, so nothing looks different at first.
- Linux: no changes in this release.

## Tested on
- Haiku R1/beta6 in a VM and on real hardware (64-bit). The 32-bit package was built but not run.
