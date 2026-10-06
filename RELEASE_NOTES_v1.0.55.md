# hDesktop v1.0.55

The biggest release since the Linux port landed. Linux gets window previews, a workspace preview with
drag-to-move, Wayfire support, a notification server and a redesigned menu selector. Haiku gets a custom
Tracker menu that looks like the Linux one, snake trail included.

## Highlights

**Linux**
- Live **window previews** on KDE Plasma: hover a dock icon to see thumbnails of its minimized windows.
- **Workspace preview**: right-click the workspace widget for a mini desktop per workspace, with a box for
  every window. Click to switch or raise, drag a box to move the window to another workspace.
  KDE Plasma, Sway, Hyprland and Wayfire.
- **Wayfire support**: the workspace widget now works there.
- A rounded, beveled menu selector with a **snake trail** that stays joined across submenus, in a
  **Selector Color** you choose.
- Built-in **notification server** with toast panels, and a **Log out** button in the app drawer.
- One dock **per Wayland session**, so KDE on one tty and another compositor on another tty can each run one.

**Haiku**
- A **custom Tracker navigation menu** replaces Tracker's own for the Tracker icon's right-click menu:
  the same look as Linux, with the snake trail, scrolling and release-to-open.
- Thumbnail previews get a **size slider**, **clickable** previews, and a title list that no longer covers them.
- A **release delay on the app drawer**: it used to close the instant the pointer left it, and now waits a
  moment first, so a brief slip off the edge doesn't lose it.

## Linux

### New
- **Window previews (KDE Plasma / KWin only, off by default).** Settings > Window Previews.
  - Hovering an icon lists its windows; each *minimized* window gets a live thumbnail (up to 30 fps) with its
    title directly above it.
  - A **Preview Size** slider (shown only while previews are on).
  - Streams close with the popup. A pixel-rate budget slows the frame rate when very large windows are
    previewed, so full-screen video doesn't bog the desktop down.
  - Needs the installed dock: KWin only hands the screencast protocol to clients whose `.desktop` file asks for
    it, so run `sudo make -f Makefile.linux install` (or install the package).
- **Workspace preview popup.** Left-click a tile to switch, right-click the widget for the preview.
  Window positions and moves come from `plasma-window-management` on KWin and from the compositor's own IPC on
  Sway, Hyprland and Wayfire. On other compositors it is a plain workspace picker.
- **Wayfire**: workspaces are read from Wayfire's IPC. Keep the `ipc`, `ipc-rules` and `vswitch` plugins
  enabled in `wayfire.ini`.
- **Menu selector**: rounded corners, a light top edge and dark bottom edge (bevel), and the **snake trail**: the
  highlight runs unbroken from the root menu through every open submenu, including while a submenu scrolls.
  - **Settings > Snake Trail** turns the trail on or off (on by default).
  - **Settings > Selector Color** sets the accent for menus, the search outline and the settings controls. The
    bevel edges and text contrast follow from it, so a light accent still reads well.
- **Notifications**: the dock serves `org.freedesktop.Notifications` and draws toast panels
  (Settings > Notifications). It queues behind any program that already provides notifications, such as
  Plasma's, rather than replacing it.
- **Log out** button between Power off and Restart in the app drawer.
- **Gradual dock zoom**, like the Haiku build.
- **Settings window**: `[?]` help tooltips on the less obvious toggles, and the toggles paired into two columns.
- **One dock per Wayland session.** The single-instance guard is now per `WAYLAND_DISPLAY`. A second dock in the
  *same* session still just exits.
- On KDE, **text and source files open with your real default** (for example Kate) instead of the first editor
  GIO finds for the exact file type.

### Fixed
- The **Tracker icon** is built at startup even when there are no windows and no workspace events, instead of
  appearing only after the first window opened (seen on Wayfire).
- **Log out** ends the graphical session even when the dock was started from a terminal or over SSH.
- The menu highlight follows the pointer: no second row stays lit while a submenu closes.
- The hover popup reopens at the right width when a window is minimized or restored (no squeezed card, no
  over-wide title bar).
- A stray hairline between a settings label and its `[?]` badge.

## Haiku

### New
- **Custom Tracker navigation menu** for the Tracker icon's right-click menu. It shows the same two rows as the
  Linux build ("/" and "Home"); hovering a folder opens its submenu.
  - Rounded popup per level, beveled selector, and the snake trail joining the levels, drawn the same way as on
    Linux.
  - The popup's background and text come from **Haiku's current color scheme**, so a light theme gives a light
    menu. The selector and trail have their own color.
  - Long folders scroll: hover the arrow at either end.
  - **Letting go of the mouse over an item opens it in Tracker**, whether it is a folder or a file.
  - Symbolic links such as `/etc`, `/bin` and `/tmp` are followed (their target's icon, contents and open
    action); links that lead nowhere are left out. `/dev` is never listed.
- **Settings > Snake Trail** and **Settings > Selector Color** (saved with the other settings).
- **Thumbnail previews**: a **Thumbnail Size** slider (120 to 360 px), clicking a preview raises that window, and
  the title list stacks above the preview instead of covering it. The preview controls moved to the bottom of the
  settings window, which now grows and shrinks to fit them.
- **App drawer release delay.** Before, Haiku closed the drawer window the instant the mouse left it. Now it waits
  about a quarter of a second first. If the pointer comes back inside, or onto the dock, in that time the drawer
  stays open; a deliberate move away still closes it. This matches the Linux build.

### Not changed
- Haiku previews still show only visible windows: Haiku keeps no off-screen copy of a minimized window to capture.

## Upgrading

**Linux**
- New dependency: **`pipewire`** (the PKGBUILD lists it). Arch / CachyOS:
  `sudo pacman -S --needed pipewire`
- Reinstall rather than running from the build folder if you use KDE previews or the taskbar:
  `sudo make -f Makefile.linux install`
- Existing settings are read as before; new options start at their defaults.
- Autostart lines for KDE/XFCE, Sway, Hyprland, labwc and Wayfire are listed in the README.

**Haiku**
- Rebuild with `make release` as usual. New settings (Snake Trail, Selector Color, Thumbnail Size) start at
  their defaults.

## Notes and limitations
- Window previews are **KWin only**. Other Wayland compositors have no way to capture a single window, so the
  toggle is greyed out there.
- Two docks in two sessions share your user D-Bus: tray icons appear in both, and only one program can serve
  notifications, so the second session's dock won't show toasts.
- On compositors with no way to report windows per workspace (labwc, for example), the workspace preview shows
  the workspaces without window boxes.

## Tested on
- KDE Plasma 6.7 (KWin) on CachyOS.
- Sway 1.12, labwc and Wayfire 0.11 on a CachyOS VM; the workspace preview was driven end to end on Sway.
- Hyprland 0.56: its window data and its move and focus commands were checked against a live instance; the popup
  itself was not viewed on a live Hyprland display.
- Haiku R1/beta6 (hrev60200) in a VM.

## Housekeeping
- The old `server/` copy of the sources (version 1.0.53, with committed binaries) was removed from the repository.
- README updated for the new features, dependencies and autostart instructions.
