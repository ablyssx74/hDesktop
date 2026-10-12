# hDesktop v1.0.63

The version number now comes from one place, and the Haiku build shows the right one.

## Highlights

**Haiku**
- **The version shown is the real one.** The About box and the update check on Haiku still said v1.0.58 in v1.0.59 to v1.0.62, because the number was kept by hand in two places and only one was updated. The update check compared it with GitHub's `VERSION`, so those releases could offer an update that was already installed. Fixed.

**Build (Haiku and Linux)**
- **A release changes one file: `VERSION`.** The Makefiles read it and pass it to the compiler, and the package name, the package version and the version in Haiku's Info window follow. A build without the Makefile stops with an error rather than carrying an old number.

## Upgrading
- Haiku: install the package for the whole system, or rebuild with `make release`. No new settings.
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`.

## Tested on
- Haiku R1/beta6 in a VM (64-bit): the build with `VERSION` set to two different values, checking the program, the package and the Info window version. Linux: the build and `hdesktop --version`. The 32-bit package was built but not run.
