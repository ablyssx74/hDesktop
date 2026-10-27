# hDesktop v1.0.72

On Haiku the dock changes its wallpaper with the same effect as the desktop, at the same moment.

## Highlights

**Haiku**
- **The dock's wallpaper follows SnakeTracker's transition.** The dock draws its own copy of the wallpaper behind its icons, and used to swap it the moment the wallpaper changed, so the desktop faded and the dock jumped. It now does the same change as the desktop: a **fade**, a **wipe** (a soft edge sweeping from left to right), a **slide** (the new picture pushes the old one out to the left) or a **circle reveal** (from the middle of the screen), with the same duration, as chosen in SnakeTracker Wallpaper (SnakeTracker 1.0.56 has the three new effects besides the fade). Tracker tells the dock when a change starts, so the two start at the same instant and run on the same clock; if it does not (an older SnakeTracker, or a change by the Backgrounds preferences), the dock starts its own after a moment, and with a duration of none it changes at once. A switch of workspace still changes at once, without an effect.

## Upgrading
- Haiku: install `hdesktop-1.0.72-1-x86_64.hpkg` (or `-x86_gcc2` on 32-bit Haiku). For the effects to match you also want SnakeTracker 1.0.56; with an older one the dock still fades, but the two are not started together.
- Linux: no change in this version apart from the version number. `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`; check with `pacman -Q hdesktop-git` that the new version is the one installed.

## Tested on
- Haiku R1/beta6, 64-bit VM: the 64-bit package of this version was installed and the dock started from it. A wallpaper change with the wipe effect (two seconds) was looked at in the middle of the change: the edge of the new picture lined up on the desktop and behind the dock. Earlier, with a test build of the dock, the wipe's edge on the desktop and on the dock was measured to be within 10 to 15 pixels; the slide and the circle reveal were compared by eye only.
- The 32-bit package was built on 32-bit Haiku and not run.
- Linux (KDE Plasma on Wayland): compile check only (the Linux dock has no change).
