# hDesktop v1.0.71

The dock's own Tracker menu has a Raised Look setting on Haiku.

## Highlights

**Haiku**
- **Raised Look for the Tracker menu.** The menu the Tracker icon opens has two looks: the pill look, with its raised, shaded selector, and an older rectangular menu with a beveled selector. Until now it took the look from Snake Tracker's preference (Tracker preferences > SnakeTracker > Raised Snake look for Tracker and Deskbar) and had no choice of its own. A **Raised Look** checkbox is now in hDesktop's settings, next to Rounded Edges. On is the pill look, off is the older rectangular menu. When hDesktop has no choice of its own saved, it starts from Snake Tracker's preference, so nothing changes until you press it. The app drawer still follows Snake Tracker's preference.
- **Flat is greyed out while Raised Look is on.** The Flat option takes away the light top edge and dark bottom edge of the selector, and only the look without the raised selector has them: the raised look shades the selector itself, so the option changed nothing you could see. It is disabled (and its label dimmed) while Raised Look is on, and keeps its value for when the raised look is turned off. This is the same as in Snake Tracker 1.0.52.

## Upgrading
- Haiku: install `hdesktop-1.0.71-1-x86_64.hpkg` (or `-x86_gcc2` on 32-bit Haiku) for the whole system. Snake Tracker 1.0.52 has the matching change in its own preferences.
- Linux: no change in this version apart from the version number. `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`; check with `pacman -Q hdesktop-git` that the new version is the one installed.

## Tested on
- Haiku R1/beta6, 64-bit VM: the 64-bit package was installed and the dock restarted from it. A test program showed the Tracker menu with Raised Look off as the older rectangular menu with a flat blue selector. The settings window's new checkbox, and the difference that Flat makes in the older look, were not looked at. The 32-bit package was built on 32-bit Haiku and not run.
- Linux (KDE Plasma on Wayland): compile check only (the Linux dock has no Tracker menu and no change).
