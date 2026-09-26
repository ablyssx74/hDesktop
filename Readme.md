# hDesktop
SDL2 OpenGL Hyrbid Desktop Manager

### Build
```
make release

```

### Linux (Wayland)

`hdesktop_linux.cpp` is a Wayland port of the dock. It needs a compositor with
`wlr-layer-shell`: KDE Plasma (KWin), Hyprland, Sway, niri, labwc, Wayfire, COSMIC.
GNOME is not supported.

Arch / CachyOS dependencies:
```
sudo pacman -S --needed base-devel wayland libglvnd mesa cairo pango librsvg gdk-pixbuf2 glib2 libpulse curl libxkbcommon
```

Build and install:
```
make -f Makefile.linux
sudo make -f Makefile.linux install      # installs /usr/bin/hdesktop
```
or `cd linux && makepkg -si` (the PKGBUILD builds from the default branch).

Start it with `hdesktop` (`-d` for debug output, `-o DP-1` to choose a monitor). To
start it with your session, copy `/usr/share/hdesktop/hdesktop-autostart.desktop` to
`~/.config/autostart/` (KDE) or add `exec-once = hdesktop` (Hyprland) / `exec hdesktop` (Sway).

On KDE Plasma, KWin only grants the window-list protocol to installed clients, so the
taskbar part of the dock needs `make install` (running from the build tree still shows
the launcher, tray, clock, volume, CPU graph and workspaces). You will probably want to
remove or auto-hide Plasma's own panel.

What maps to what on Linux:

| Dock feature | Wayland / Linux implementation |
|---|---|
| Taskbar (list, raise, minimize, close) | `org_kde_plasma_window_management` (KWin) or `wlr-foreign-toplevel-management` (Hyprland, Sway, ...) |
| Workspace switcher | KDE virtual desktops, `ext-workspace-v1`, Hyprland IPC or Sway IPC |
| Icons | freedesktop icon theme (follows your KDE/GTK theme) + `.desktop` files |
| App drawer | all installed `.desktop` applications, favorites, type-to-search |
| System tray | StatusNotifierItem + dbusmenu (D-Bus) |
| Volume | PulseAudio API (PipeWire's `pipewire-pulse`) |
| CPU / memory / process menus | `/proc` |
| Trash | freedesktop Trash (`~/.local/share/Trash`) |
| Tracker folder menus | "Places" submenus that browse the file system |

Not ported (yet): live window thumbnails and the drag-windows-between-workspaces
preview, which have no generic Wayland equivalent.

Settings are stored in `~/.config/hdesktop/settings.ini`. That file also has
`clock_command`, `mixer_command` and `icon_theme` keys to override auto-detection.

### Screenshots
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/74c650b0-0b77-4c64-a490-bb777a73a47e" />


