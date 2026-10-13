# hDesktop v1.0.67

The dock keeps clear of your Deskbar (Haiku).

## Highlights

**Haiku**
- **The dock moves off the Deskbar's edge.** When hdesktop starts it finds where the Deskbar is. If the Deskbar is along the top of the screen (all across it, or in either top corner) and the dock is set to the top, the dock goes to the bottom; the same the other way round for a Deskbar at the bottom. A Deskbar on the left or right edge leaves the dock where it was set. It is done at start-up, and waits a few seconds for the Deskbar if it is still starting.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: no changes in this release.

## Tested on
- Haiku R1/beta6 in a VM (64-bit), with the Deskbar at the top and at the bottom. The 32-bit package was built but not run.
