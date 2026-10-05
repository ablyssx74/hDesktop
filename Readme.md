# hDesktop
SDL2 OpenGL Hybrid Haiku OS Inspired Desktop Manager 

### Haiku (32/64bit)
```
make release

```

### Linux (Wayland)

`hdesktop_linux.cpp` is a Wayland port of the dock. It needs a compositor with
`wlr-layer-shell`: KDE Plasma (KWin), Hyprland, Sway, niri, labwc, Wayfire, COSMIC.
GNOME is not supported.

Arch / CachyOS dependencies:
```
sudo pacman -S --needed base-devel wayland libglvnd mesa cairo pango librsvg gdk-pixbuf2 glib2 libpulse curl libxkbcommon pipewire
```

Build and install:
```
make -f Makefile.linux
sudo make -f Makefile.linux install      # installs /usr/bin/hdesktop
```
or `cd linux && makepkg -si` (the PKGBUILD builds from the default branch).

Start it with `hdesktop` (`-d` for debug output, `-o DP-1` to choose a monitor). One dock
runs per Wayland session, so you can run it in several sessions at once (KDE on one tty,
Sway on another); starting a second one in the same session just exits. Sessions share your
user D-Bus and `settings.ini`: tray icons show in every dock, and only one program can
serve notifications, so the other sessions' docks won't show toasts. To start it with your
session:

| Session | How |
|---|---|
| KDE Plasma, XFCE, GNOME | copy `/usr/share/hdesktop/hdesktop-autostart.desktop` to `~/.config/autostart/` |
| Sway | add `exec hdesktop` to `~/.config/sway/config` |
| Hyprland | add `exec-once = hdesktop` to `~/.config/hypr/hyprland.conf` |
| labwc | add `hdesktop >/dev/null 2>&1 &` to `~/.config/labwc/autostart` (XFCE's Wayland session: `~/.config/xfce4/labwc/autostart`) |
| Wayfire | add `hdesktop = hdesktop` under `[autostart]` in `~/.config/wayfire.ini` |

Wayfire has no workspace protocol, so the workspace widget reads its IPC: keep the `ipc`,
`ipc-rules` and `vswitch` plugins enabled in `wayfire.ini`.

On KDE Plasma, KWin only grants the window-list protocol to installed clients, so the
taskbar part of the dock needs `make install` (running from the build tree still shows
the launcher, tray, clock, volume, CPU graph and workspaces). You will probably want to
remove or auto-hide Plasma's own panel.

What maps to what on Linux:

| Dock feature | Wayland / Linux implementation |
|---|---|
| Taskbar (list, raise, minimize, close) | `org_kde_plasma_window_management` (KWin) or `wlr-foreign-toplevel-management` (Hyprland, Sway, ...) |
| Workspace switcher | KDE virtual desktops, `ext-workspace-v1`, Hyprland IPC, Sway IPC or Wayfire IPC. Left-click a tile to switch; right-click opens the workspace preview (below) |
| Workspace preview | Every workspace as a mini desktop with a box per window: click a mini desktop to switch, click a box to raise that window, drag a box to another workspace to move it. Window positions and moves come from `plasma-window-management` (KWin) or the compositor's IPC (Sway, Hyprland, Wayfire); on other compositors it is a workspace picker without window boxes |
| Window previews | Live thumbnails of minimized windows in the icon's hover list (off by default; Settings > Window Previews, with a size slider). KWin only: `zkde_screencast` + PipeWire |
| Icons | freedesktop icon theme (follows your KDE/GTK theme) + `.desktop` files |
| App drawer | all installed `.desktop` applications, favorites, type-to-search |
| System tray | StatusNotifierItem + dbusmenu (D-Bus) |
| Volume | PulseAudio API (PipeWire's `pipewire-pulse`) |
| CPU / memory / process menus | `/proc` |
| Trash | freedesktop Trash (`~/.local/share/Trash`) |
| Tracker | A permanent Tracker icon for your default file manager (its windows group under it); right-click for "/" and "Home" folder menus: hover a folder to open it as a submenu, click a folder or file to open it |

Window previews are KWin only: other Wayland compositors have no way to capture a single
window, so the Window Previews toggle is greyed out there. Everything else works on every
supported compositor, with the workspace preview's window boxes as described above.

Menus use a rounded, beveled selector that stays joined across submenus (the "snake trail",
Settings > Snake Trail) in an accent color you choose (Settings > Selector Color).

Settings are stored in `~/.config/hdesktop/settings.ini`. That file also has
`clock_command`, `mixer_command` and `icon_theme` keys to override auto-detection.

### Screenshots
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/74c650b0-0b77-4c64-a490-bb777a73a47e" />
<img width="1920" height="1080" alt="Image" src="https://github.com/user-attachments/assets/7e525ce9-7770-49a8-bb52-c9553db015f8" />


