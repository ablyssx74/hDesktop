# hDesktop v1.0.69

The CPU graph's Memory usage menu is ProcessController's on Haiku, and the Linux dock's labels are pills.

## Highlights

**Haiku**
- **The Memory usage menu is the one ProcessController has.** It was three rows of percentages (used, free, total). It is now "System resources & caches..." followed by a row for every team: the team's icon and name, the memory it has written to and all the memory it has, and a bar of the two against the memory in use (written: dark blue, the rest: light blue). The system row's bar shows what is cached (dark blue), what else is in use (lavender) and what is free (white). The numbers are worked out the way ProcessController does, and the rows follow teams as they start and end.

**Linux**
- **The date, volume and hover labels are pills.** Their ends are fully round now (they were rounded rectangles), as on Haiku.
- **The CPU graph's hover label is a pill** with a round outline, not a square box.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM (64-bit): the Memory usage menu opened from the CPU graph. The 32-bit package was built but not run. The Linux change was built, but not run on a desktop before release.
