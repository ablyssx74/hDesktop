// What hdesktop and its tray helper (the item it puts in the Deskbar's tray) have in common.
#ifndef HDESKTOP_TRAY_HELPER_H
#define HDESKTOP_TRAY_HELPER_H

#define HDESKTOP_TRAY_HELPER_NAME "hDesktopTrayHelper"
#define HDESKTOP_TRAY_HELPER_SIGNATURE "application/x-vnd.hdesktop-trayhelper"

// Raised whenever the helper or its messages change: hdesktop then takes an older helper out of the Deskbar and puts
// its own one in.
static const int32 kTrayHelperVersion = 3;

// The request: "name" (string, once for each tray item wanted), "scale" (float) and, while the pointer rests on an item,
// "tip" (string, its name) to have its tool tip's text too.
static const uint32 kTrayHelperCapture = 'hTCp';
// The reply: "version" (int32), and for each item that could be drawn "name" (string), "width" and "height" (int32),
// "back" (int32: its background colors as 0x00RRGGBB, three of them) and "pixels" (B_RAW_TYPE, B_RGBA32 rows), drawn at
// the requested scale, and "swidth", "sheight" and "small" (the same picture at the item's own size). "tip" (string) is the
// text of the tool tip that was asked for, when the item has one made of text.
static const uint32 kTrayHelperPictures = 'hTCr';

#endif
