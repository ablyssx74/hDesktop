# hDesktop v1.0.60

The Linux dock's menus get the pill look and the raised snake trail that the Haiku build has had since v1.0.58 and v1.0.59.

## Highlights

**Linux**
- **Menus are pills.** Every menu level is one solid rounded pill in a light tint of the **Selector Color**, with dark text: the Tracker folder menu, the leaf and trash menus, the tray and CPU menus, and the window title list. Separators are thin tinted pills, disabled items are muted, and the scroll arrows of long lists follow the same colours.
- **The trail is a raised bar.** The lit row and the snake trail that runs from menu to menu are the Selector Color itself, rounded and shaded like a round bar lit from the top left: lighter on top, darker underneath, with soft ends and bends the same radius as the pill's ends. Turn **Snake Trail** off in Settings and each menu keeps its own lit row, still raised.
- **Submenus overlap their parent.** Each submenu opens 20 pixels over its parent, which covers the parent's arrow and keeps the trail unbroken from level to level. The corner that touches the parent is square, and so is the bottom one when the submenu ends within its parent's height.
- **No arrow under the snake.** An arrow that the trail's vertical bar passes through is not drawn; arrows on the other side stay.
- The old tabs that stuck out of the menu's edge and the one pixel outline are gone: the pill needs neither.

## Haiku
- No changes in this release; the version number is the same as Linux's, as before.

## Upgrading
- Linux: `cd linux && makepkg -si`, or `sudo make -f Makefile.linux install`. No new settings.
- Haiku: install the package for the whole system, or rebuild with `make release`.

## Tested on
- Linux (KDE Plasma on Wayland, CachyOS): the Tracker folder menu four levels deep with the trail through all of them, the leaf and trash menus, and a long folder list with its scroll arrow. The window title list, menus that open to the left near a screen edge, and the tray and CPU menus were not looked at before release.
