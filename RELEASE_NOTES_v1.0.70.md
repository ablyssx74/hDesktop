# hDesktop v1.0.70

The Linux dock's CPU graph has ProcessController's Memory usage menu.

## Highlights

**Linux**
- **The Memory usage menu is ProcessController's.** It was three rows of percentages (used, free, total). It is now "System resources & caches..." followed by a row for every process: the process's icon and name, the memory it has written to and all the RAM it has, and a bar of the two against the memory in use (written: dark blue, the rest: light blue). The system row's bar shows what is cached (dark blue), what else is in use (lavender) and what is free (white). The rows are sorted with the biggest process first. The numbers come from `/proc`: all the RAM is the process's resident memory, and what it has written to is its private and shared anonymous memory.

## Upgrading
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`. Check with `pacman -Q hdesktop-git` that the new version is the one installed.
- Haiku: no changes in this release.

## Tested on
- Linux (KDE Plasma on Wayland): the Memory usage menu and the rounded date, tray and CPU labels from v1.0.69, run from the build tree.
