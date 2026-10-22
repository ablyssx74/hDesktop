# hDesktop v1.0.70

The Linux dock's CPU graph has ProcessController's Memory usage menu.

## Highlights

**Linux**
- **The Memory usage menu is ProcessController's.** It was three rows of percentages (used, free, total). It is now "System resources & caches..." followed by a row for every process: the process's icon and name, the memory it has written to and all the RAM it has, and a bar of the two against the memory in use (written: dark blue, the rest: light blue). The system row's bar shows what is cached (dark blue), what else is in use (lavender) and what is free (white). The rows are sorted with the biggest process first. The numbers come from `/proc`: all the RAM is the process's resident memory, and what it has written to is its private and shared anonymous memory.

## Upgrading
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`. Check with `pacman -Q hdesktop-git` that the new version is the one installed.
- Haiku: the 64-bit and 32-bit packages of this version carry v1.0.69's Memory usage menu; nothing else changed.

## Tested on
- Haiku R1/beta6: the 64-bit and 32-bit packages were built (on 64-bit Haiku and on 32-bit Haiku). v1.0.69's Memory usage menu was run in a 64-bit VM, from a build and not from the package. The 32-bit package was not run.
- Linux (KDE Plasma on Wayland): the Memory usage menu and the rounded date, tray and CPU labels from v1.0.69, run from the build tree.
