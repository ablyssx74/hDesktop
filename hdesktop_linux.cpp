/*
 * Copyright 2026, Kris Beazley hDesktop@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 *
 * hdesktop_linux.cpp -- the Wayland port of hDesktop's dock.
 *
 * The Haiku build (hdesktop.cpp) talks to app_server, the roster, Deskbar and
 * Tracker directly. None of that exists on Linux, and Wayland deliberately
 * gives ordinary clients no way to see or steer other applications' windows,
 * so this port is built on the compositor-specific protocols that docks and
 * panels use instead:
 *
 *   dock / drawer / settings surfaces  zwlr_layer_shell_v1 (KWin, Hyprland, Sway,
 *                                      niri, labwc, Wayfire, COSMIC ...)
 *   taskbar (list/raise/minimize/close) zwlr_foreign_toplevel_manager_v1 (wlroots
 *                                      family, Hyprland) or
 *                                      org_kde_plasma_window_management (KWin)
 *   workspace switcher                 org_kde_plasma_virtual_desktop_management,
 *                                      ext_workspace_manager_v1, or the Hyprland /
 *                                      Sway IPC sockets
 *   system tray                        StatusNotifierItem + com.canonical.dbusmenu
 *                                      over D-Bus
 *   volume                             PulseAudio API (served by pipewire-pulse)
 *   icons                              freedesktop icon theme spec + .desktop files
 *
 * The dock itself is still drawn with legacy OpenGL exactly like the Haiku
 * build (same layout math, hover magnification and open/close effects), just
 * through EGL on a layer surface instead of SDL2. Menus, the app drawer and the
 * settings panel are drawn with cairo/pango into shared-memory buffers.
 *
 * GNOME/Mutter implements neither layer-shell nor a toplevel-management
 * protocol, so hDesktop can't run there.
 */

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <cairo.h>
#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <curl/curl.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "xdg-shell-client-protocol.h"
// The generated header names a parameter `namespace`, a C++ keyword.
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "plasma-window-management-client-protocol.h"
#include "org-kde-plasma-virtual-desktop-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#define APP_LOCAL_VERSION "v1.0.51"

// Linux input event codes (linux/input-event-codes.h), spelled out so the
// build doesn't depend on kernel headers being installed.
#ifndef BTN_LEFT
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112
#endif

// Mirrors the SDL_BUTTON_* numbering the Haiku build's click handlers use.
enum MouseButton { kButtonNone = 0, kButtonLeft = 1, kButtonMiddle = 2, kButtonRight = 3 };

// =========================================================================
// DEBUG LOGGING (-d / --debug)
// =========================================================================
static bool gDebugEnabled = false;

static void DebugLog(const char* fmt, ...) {
    if (!gDebugEnabled) return;
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[hdesktop] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

static void WarnLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[hdesktop] ");
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

static uint64_t NowMs() {
    return static_cast<uint64_t>(g_get_monotonic_time() / 1000);
}

// =========================================================================
// SETTINGS
// =========================================================================
// Same options as the Haiku build's ConfigView, minus the ones that only mean
// something on app_server (window preview thumbnails and the direct
// framebuffer capture behind them). Stored as a plain key file in
// $XDG_CONFIG_HOME/hdesktop/settings.ini instead of a flattened BMessage.
enum DockLocation {
    kDockLocationBottom = 0,
    kDockLocationTop    = 1
};

enum IconEffect {
    kEffectNone = 0,
    kEffectBounce,
    kEffectSpin,
    kEffectIllusion,
    kEffectWobble,
    kEffectExplode,
    kEffectCount
};

static const char* const kEffectNames[kEffectCount] = {
    "None", "Bounce", "Spin", "Illusion", "Wobble", "Explode"
};

struct Settings {
    bool   autoHide = false;
    bool   showSystemTray = true;
    bool   keepAboveWindows = true;   // Haiku: "auto-raise" (floating feel on hover)
    bool   reserveSpace = true;       // Wayland only: exclusive zone so maximized windows stop at the dock
    bool   titlePopup = true;         // Haiku mode title overlay: clickable window list popup
    bool   titleLabel = false;        // SDL mode title overlay: single label + hover auto-focus
    bool   workspaceSwitcher = true;
    bool   clock24h = false;
    int    dockLocation = kDockLocationBottom;
    float  baseIconSize = 48.0f;
    float  dockAlpha = 0.32f;
    int    effectDurationMs = 750;
    int    openEffect = kEffectSpin;
    int    closeEffect = kEffectNone;
    std::set<std::string> favorites;  // desktop file ids, e.g. "org.kde.dolphin.desktop"
    std::string output;               // wl_output name to put the dock on; empty = compositor's choice
    std::string iconTheme;            // empty = follow the desktop's theme
    // Click actions for widgets whose Haiku targets (Time / Media preferences,
    // Tracker) have no single Linux equivalent. Empty = auto-detect.
    std::string clockCommand;
    std::string mixerCommand;
    std::string terminalCommand;
    bool   checkForUpdates = true;
};

static Settings gSettings;

static std::string ConfigDir() {
    return std::string(g_get_user_config_dir()) + "/hdesktop";
}

static std::string SettingsPath() {
    return ConfigDir() + "/settings.ini";
}

static void SaveConfiguration() {
    g_mkdir_with_parents(ConfigDir().c_str(), 0755);
    GKeyFile* kf = g_key_file_new();
    const char* g = "Dock";
    g_key_file_set_boolean(kf, g, "auto_hide", gSettings.autoHide);
    g_key_file_set_boolean(kf, g, "system_tray", gSettings.showSystemTray);
    g_key_file_set_boolean(kf, g, "keep_above_windows", gSettings.keepAboveWindows);
    g_key_file_set_boolean(kf, g, "reserve_space", gSettings.reserveSpace);
    g_key_file_set_boolean(kf, g, "title_popup", gSettings.titlePopup);
    g_key_file_set_boolean(kf, g, "title_label", gSettings.titleLabel);
    g_key_file_set_boolean(kf, g, "workspace_switcher", gSettings.workspaceSwitcher);
    g_key_file_set_boolean(kf, g, "clock_24h", gSettings.clock24h);
    g_key_file_set_integer(kf, g, "dock_location", gSettings.dockLocation);
    g_key_file_set_double(kf, g, "base_icon_size", gSettings.baseIconSize);
    g_key_file_set_double(kf, g, "dock_alpha", gSettings.dockAlpha);
    g_key_file_set_integer(kf, g, "effect_duration", gSettings.effectDurationMs);
    g_key_file_set_string(kf, g, "open_effect", kEffectNames[gSettings.openEffect]);
    g_key_file_set_string(kf, g, "close_effect", kEffectNames[gSettings.closeEffect]);
    g_key_file_set_string(kf, g, "output", gSettings.output.c_str());
    g_key_file_set_string(kf, g, "icon_theme", gSettings.iconTheme.c_str());
    g_key_file_set_string(kf, g, "clock_command", gSettings.clockCommand.c_str());
    g_key_file_set_string(kf, g, "mixer_command", gSettings.mixerCommand.c_str());
    g_key_file_set_string(kf, g, "terminal_command", gSettings.terminalCommand.c_str());
    g_key_file_set_boolean(kf, g, "check_for_updates", gSettings.checkForUpdates);

    std::vector<const char*> favs;
    for (const auto& f : gSettings.favorites) favs.push_back(f.c_str());
    g_key_file_set_string_list(kf, "Drawer", "favorites", favs.data(), favs.size());

    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, SettingsPath().c_str(), &err)) {
        WarnLog("could not save settings: %s\n", err ? err->message : "?");
        g_clear_error(&err);
    }
    g_key_file_free(kf);
}

static int EffectFromName(const char* name, int fallback) {
    if (name == nullptr) return fallback;
    for (int i = 0; i < kEffectCount; ++i) {
        if (g_ascii_strcasecmp(name, kEffectNames[i]) == 0) return i;
    }
    return fallback;
}

static void LoadConfiguration() {
    GKeyFile* kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, SettingsPath().c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_free(kf);
        return;
    }
    const char* g = "Dock";
    auto getBool = [&](const char* key, bool& out) {
        GError* e = nullptr;
        gboolean v = g_key_file_get_boolean(kf, g, key, &e);
        if (e == nullptr) out = v; else g_error_free(e);
    };
    auto getInt = [&](const char* key, int& out) {
        GError* e = nullptr;
        int v = g_key_file_get_integer(kf, g, key, &e);
        if (e == nullptr) out = v; else g_error_free(e);
    };
    auto getFloat = [&](const char* key, float& out) {
        GError* e = nullptr;
        double v = g_key_file_get_double(kf, g, key, &e);
        if (e == nullptr) out = static_cast<float>(v); else g_error_free(e);
    };
    auto getString = [&](const char* key, std::string& out) {
        char* v = g_key_file_get_string(kf, g, key, nullptr);
        if (v != nullptr) { out = v; g_free(v); }
    };

    getBool("auto_hide", gSettings.autoHide);
    getBool("system_tray", gSettings.showSystemTray);
    getBool("keep_above_windows", gSettings.keepAboveWindows);
    getBool("reserve_space", gSettings.reserveSpace);
    getBool("title_popup", gSettings.titlePopup);
    getBool("title_label", gSettings.titleLabel);
    getBool("workspace_switcher", gSettings.workspaceSwitcher);
    getBool("clock_24h", gSettings.clock24h);
    getInt("dock_location", gSettings.dockLocation);
    getFloat("base_icon_size", gSettings.baseIconSize);
    getFloat("dock_alpha", gSettings.dockAlpha);
    getInt("effect_duration", gSettings.effectDurationMs);
    std::string effect;
    getString("open_effect", effect);
    gSettings.openEffect = EffectFromName(effect.c_str(), gSettings.openEffect);
    effect.clear();
    getString("close_effect", effect);
    gSettings.closeEffect = EffectFromName(effect.c_str(), gSettings.closeEffect);
    getString("output", gSettings.output);
    getString("icon_theme", gSettings.iconTheme);
    getString("clock_command", gSettings.clockCommand);
    getString("mixer_command", gSettings.mixerCommand);
    getString("terminal_command", gSettings.terminalCommand);
    getBool("check_for_updates", gSettings.checkForUpdates);

    gsize count = 0;
    char** favs = g_key_file_get_string_list(kf, "Drawer", "favorites", &count, nullptr);
    if (favs != nullptr) {
        gSettings.favorites.clear();
        for (gsize i = 0; i < count; ++i) {
            if (favs[i][0] != '\0') gSettings.favorites.insert(favs[i]);
        }
        g_strfreev(favs);
    }
    g_key_file_free(kf);

    // Same clamps the Haiku sliders enforce.
    gSettings.dockLocation = (gSettings.dockLocation == kDockLocationTop) ? kDockLocationTop : kDockLocationBottom;
    gSettings.baseIconSize = std::clamp(gSettings.baseIconSize, 32.0f, 72.0f);
    gSettings.dockAlpha = std::clamp(gSettings.dockAlpha, 0.0f, 1.0f);
    gSettings.effectDurationMs = std::clamp(gSettings.effectDurationMs, 200, 1500);
    if (gSettings.titlePopup && gSettings.titleLabel) gSettings.titleLabel = false;
}

// =========================================================================
// SMALL UTILITIES
// =========================================================================
static std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(g_ascii_tolower(c));
    return s;
}

static bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

static bool IsDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string ReadFile(const std::string& path) {
    char* contents = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(path.c_str(), &contents, &len, nullptr)) return std::string();
    std::string result(contents, len);
    g_free(contents);
    return result;
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string HomeDir() {
    return g_get_home_dir();
}

// Whether an executable is on $PATH.
static bool HaveProgram(const char* name) {
    char* found = g_find_program_in_path(name);
    bool ok = (found != nullptr);
    g_free(found);
    return ok;
}

// Runs a shell command fully detached from the dock (double fork + setsid) so
// it never becomes our zombie and survives the dock restarting -- the Linux
// equivalent of the Haiku build's `std::system("... &")` calls.
// XDG_ACTIVATION_TOKEN is passed along when we have one, so compositors with
// focus-stealing prevention (KWin) let the new window take focus.
static void RunDetached(const std::string& command, const std::string& activationToken = std::string()) {
    if (command.empty()) return;
    DebugLog("run: %s\n", command.c_str());
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        setsid();
        pid_t grandchild = fork();
        if (grandchild != 0) _exit(0);
        if (!activationToken.empty()) {
            setenv("XDG_ACTIVATION_TOKEN", activationToken.c_str(), 1);
            setenv("DESKTOP_STARTUP_ID", activationToken.c_str(), 1);
        } else {
            unsetenv("XDG_ACTIVATION_TOKEN");
            unsetenv("DESKTOP_STARTUP_ID");
        }
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (!gDebugEnabled) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
            }
            if (devnull > 2) close(devnull);
        }
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

static std::string ShellQuote(const std::string& s) {
    char* q = g_shell_quote(s.c_str());
    std::string r(q);
    g_free(q);
    return r;
}

static bool DesktopIs(const char* name) {
    const char* cur = g_getenv("XDG_CURRENT_DESKTOP");
    if (cur == nullptr) return false;
    std::string lower = ToLower(cur);
    return lower.find(ToLower(name)) != std::string::npos;
}

// =========================================================================
// PANGO / CAIRO TEXT RASTERIZATION
// =========================================================================
// Replaces BFont + BView::DrawString into an offscreen BBitmap. Sizes are
// treated as pixels (like be_plain_font sizes), not points.
struct RGBA {
    double r = 0, g = 0, b = 0, a = 1;
};

static PangoLayout* MakeLayout(cairo_t* cr, const std::string& text, double pixelSize, bool bold) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(desc, pixelSize * PANGO_SCALE);
    if (bold) pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout, text.c_str(), -1);
    return layout;
}

// Measures text in logical pixels without needing a real target surface.
static void MeasureText(const std::string& text, double pixelSize, bool bold, double* outW, double* outH) {
    static cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(scratch);
    PangoLayout* layout = MakeLayout(cr, text, pixelSize, bold);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    cairo_destroy(cr);
    if (outW) *outW = w;
    if (outH) *outH = h;
}

// Draws text with its top-left at (x, y), optionally ellipsized to maxWidth.
static void DrawText(cairo_t* cr, const std::string& text, double x, double y, double pixelSize, bool bold,
    RGBA color, double maxWidth = -1.0, bool center = false) {
    PangoLayout* layout = MakeLayout(cr, text, pixelSize, bold);
    if (maxWidth > 0) {
        pango_layout_set_width(layout, static_cast<int>(maxWidth * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        if (center) pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    }
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

static void RoundedRectPath(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// A premultiplied ARGB32 image plus its logical (unscaled) size -- what gets
// uploaded as a GL texture for the dock, or painted directly by cairo panels.
struct Image {
    cairo_surface_t* surface = nullptr; // device pixels
    double logicalW = 0, logicalH = 0;
};

static void FreeImage(Image& img) {
    if (img.surface) cairo_surface_destroy(img.surface);
    img.surface = nullptr;
}

// RenderTextToTexture() equivalent: plain text, transparent background.
static Image RenderTextImage(const std::string& text, double pixelSize, bool bold, RGBA color, double scale,
    double padX = 3.0, double padY = 2.0) {
    double tw = 0, th = 0;
    MeasureText(text, pixelSize, bold, &tw, &th);
    Image img;
    img.logicalW = std::ceil(tw + padX * 2);
    img.logicalH = std::ceil(th + padY * 2);
    int pw = std::max(1, static_cast<int>(std::ceil(img.logicalW * scale)));
    int ph = std::max(1, static_cast<int>(std::ceil(img.logicalH * scale)));
    img.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    cairo_t* cr = cairo_create(img.surface);
    cairo_scale(cr, scale, scale);
    DrawText(cr, text, padX, padY, pixelSize, bold, color);
    cairo_destroy(cr);
    cairo_surface_flush(img.surface);
    return img;
}

// RenderWhiteTextToTexture() equivalent: bold white text on a dark rounded
// capsule (hover titles, the clock's date tooltip).
static Image RenderCapsuleTextImage(const std::string& text, double pixelSize, double scale) {
    double tw = 0, th = 0;
    MeasureText(text, pixelSize, true, &tw, &th);
    Image img;
    img.logicalW = std::ceil(tw + 16.0);
    img.logicalH = std::ceil(th + 10.0);
    int pw = std::max(1, static_cast<int>(std::ceil(img.logicalW * scale)));
    int ph = std::max(1, static_cast<int>(std::ceil(img.logicalH * scale)));
    img.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
    cairo_t* cr = cairo_create(img.surface);
    cairo_scale(cr, scale, scale);
    RoundedRectPath(cr, 0, 0, img.logicalW, img.logicalH, 5.0);
    cairo_set_source_rgba(cr, 15 / 255.0, 15 / 255.0, 15 / 255.0, 190 / 255.0);
    cairo_fill(cr);
    DrawText(cr, text, 8.0, 5.0, pixelSize, true, RGBA{1, 1, 1, 1});
    cairo_destroy(cr);
    cairo_surface_flush(img.surface);
    return img;
}

// =========================================================================
// FREEDESKTOP ICON THEME LOOKUP
// =========================================================================
// Replaces BNodeInfo::GetTrackerIcon() / BEOS:ICON vector icons. Implements
// the icon theme spec's lookup: the user's theme, its Inherits chain, then
// hicolor, then /usr/share/pixmaps, picking the directory whose size is
// closest to what's requested. SVGs are rasterized with librsvg at exactly the
// requested pixel size, so dock icons stay sharp through the hover zoom.

// Tiny, forgiving INI reader: KDE's kdeglobals uses syntax GKeyFile rejects.
static std::map<std::string, std::map<std::string, std::string>> ReadIni(const std::string& path) {
    std::map<std::string, std::map<std::string, std::string>> result;
    std::string contents = ReadFile(path);
    std::string group;
    size_t pos = 0;
    while (pos < contents.size()) {
        size_t eol = contents.find('\n', pos);
        if (eol == std::string::npos) eol = contents.size();
        std::string line = Trim(contents.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            group = line.substr(1, line.size() - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        size_t bracket = key.find("[$");
        if (bracket != std::string::npos) key = key.substr(0, bracket);
        result[group][key] = Trim(line.substr(eq + 1));
    }
    return result;
}

static std::vector<std::string> SplitList(const std::string& s, char sep = ',') {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        std::string part = Trim(s.substr(start, end - start));
        if (!part.empty()) out.push_back(part);
        start = end + 1;
    }
    return out;
}

class IconTheme {
public:
    IconTheme() {
        std::string dataHome = g_get_user_data_dir();
        fBaseDirs.push_back(dataHome + "/icons");
        fBaseDirs.push_back(HomeDir() + "/.icons");
        for (const char* const* d = g_get_system_data_dirs(); d && *d; ++d) {
            fBaseDirs.push_back(std::string(*d) + "/icons");
        }
        fPixmapDirs.push_back("/usr/share/pixmaps");
        fThemeName = DetectThemeName();
        DebugLog("icon theme: %s\n", fThemeName.c_str());
    }

    const std::string& ThemeName() const { return fThemeName; }

    // Extra directory to search first (StatusNotifierItem's IconThemePath).
    std::string LookupPath(const std::string& name, int size, const std::string& extraDir = std::string()) {
        if (name.empty()) return std::string();
        if (name[0] == '/') return FileExists(name) ? name : std::string();

        std::string key = name + "@" + std::to_string(size) + "@" + extraDir;
        auto cached = fCache.find(key);
        if (cached != fCache.end()) return cached->second;

        std::string found;
        if (!extraDir.empty()) found = FindInFlatDir(extraDir, name, true);
        if (found.empty()) found = FindInThemes(name, size);
        if (found.empty()) {
            for (const auto& dir : fPixmapDirs) {
                found = FindInFlatDir(dir, name, false);
                if (!found.empty()) break;
            }
        }
        fCache[key] = found;
        return found;
    }

    // Loads a themed icon name or an absolute path into a square image of
    // pxSize device pixels. Falls back through the spec's "-" generic names
    // ("user-trash-full" -> "user-trash"). Returns nullptr when nothing matches.
    cairo_surface_t* Load(const std::string& name, int pxSize, const std::string& extraDir = std::string()) {
        std::string candidate = name;
        while (!candidate.empty()) {
            std::string path = LookupPath(candidate, pxSize, extraDir);
            if (!path.empty()) {
                cairo_surface_t* s = LoadFile(path, pxSize);
                if (s) return s;
            }
            size_t dash = candidate.rfind('-');
            if (dash == std::string::npos || candidate[0] == '/') break;
            candidate = candidate.substr(0, dash);
        }
        return nullptr;
    }

    // Rasterizes an image file into a centered, aspect-preserving square.
    static cairo_surface_t* LoadFile(const std::string& path, int pxSize) {
        if (pxSize <= 0) return nullptr;
        std::string lower = ToLower(path);
        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pxSize, pxSize);
        cairo_t* cr = cairo_create(out);
        bool ok = false;

        if (g_str_has_suffix(lower.c_str(), ".svg") || g_str_has_suffix(lower.c_str(), ".svgz")) {
            GError* err = nullptr;
            RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
            if (handle != nullptr) {
                RsvgRectangle viewport = {0, 0, static_cast<double>(pxSize), static_cast<double>(pxSize)};
                ok = rsvg_handle_render_document(handle, cr, &viewport, nullptr);
                g_object_unref(handle);
            }
            g_clear_error(&err);
        } else if (g_str_has_suffix(lower.c_str(), ".png")) {
            cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
            if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
                PaintScaled(cr, png, pxSize);
                ok = true;
            }
            cairo_surface_destroy(png);
        } else {
            // Anything else (xpm, jpg, ...) goes through gdk-pixbuf, which
            // librsvg already pulls in.
            ok = LoadWithPixbuf(cr, path, pxSize);
        }

        cairo_destroy(cr);
        if (!ok) {
            cairo_surface_destroy(out);
            return nullptr;
        }
        cairo_surface_flush(out);
        return out;
    }

    static void PaintScaled(cairo_t* cr, cairo_surface_t* src, int pxSize) {
        int w = cairo_image_surface_get_width(src);
        int h = cairo_image_surface_get_height(src);
        if (w <= 0 || h <= 0) return;
        double s = std::min(static_cast<double>(pxSize) / w, static_cast<double>(pxSize) / h);
        cairo_save(cr);
        cairo_translate(cr, (pxSize - w * s) / 2.0, (pxSize - h * s) / 2.0);
        cairo_scale(cr, s, s);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
        cairo_paint(cr);
        cairo_restore(cr);
    }

private:
    struct ThemeDir {
        std::string subdir;
        int size = 48, scale = 1, minSize = 48, maxSize = 48, threshold = 2;
        enum { kFixed, kScalable, kThreshold } type = kThreshold;
    };
    struct Theme {
        bool valid = false;
        std::vector<std::string> roots;     // every base dir that has this theme
        std::vector<std::string> inherits;
        std::vector<ThemeDir> dirs;
    };

    static bool LoadWithPixbuf(cairo_t* cr, const std::string& path, int pxSize);

    std::string DetectThemeName() {
        if (const char* env = g_getenv("HDESKTOP_ICON_THEME")) return env;
        if (!gSettings.iconTheme.empty()) return gSettings.iconTheme;

        std::string configHome = g_get_user_config_dir();
        auto fromKde = [&]() -> std::string {
            auto ini = ReadIni(configHome + "/kdeglobals");
            auto g = ini.find("Icons");
            if (g != ini.end()) {
                auto k = g->second.find("Theme");
                if (k != g->second.end() && !k->second.empty()) return k->second;
            }
            return std::string();
        };
        auto fromGtk = [&]() -> std::string {
            for (const char* file : {"/gtk-4.0/settings.ini", "/gtk-3.0/settings.ini"}) {
                auto ini = ReadIni(configHome + file);
                auto g = ini.find("Settings");
                if (g != ini.end()) {
                    auto k = g->second.find("gtk-icon-theme-name");
                    if (k != g->second.end() && !k->second.empty()) return k->second;
                }
            }
            GSettingsSchemaSource* src = g_settings_schema_source_get_default();
            GSettingsSchema* schema = src ? g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE) : nullptr;
            if (schema != nullptr) {
                std::string name;
                if (g_settings_schema_has_key(schema, "icon-theme")) {
                    GSettings* gs = g_settings_new("org.gnome.desktop.interface");
                    char* v = g_settings_get_string(gs, "icon-theme");
                    if (v) { name = v; g_free(v); }
                    g_object_unref(gs);
                }
                g_settings_schema_unref(schema);
                return name;
            }
            return std::string();
        };

        std::string name;
        if (DesktopIs("KDE")) name = fromKde();
        if (name.empty()) name = fromGtk();
        if (name.empty()) name = fromKde();
        if (name.empty() || !GetTheme(name).valid) {
            for (const char* fallback : {"breeze", "Papirus", "Adwaita", "hicolor"}) {
                if (GetTheme(fallback).valid) return fallback;
            }
            return "hicolor";
        }
        return name;
    }

    Theme& GetTheme(const std::string& name) {
        auto it = fThemes.find(name);
        if (it != fThemes.end()) return it->second;
        Theme& theme = fThemes[name];
        std::string indexPath;
        for (const auto& base : fBaseDirs) {
            std::string root = base + "/" + name;
            if (!IsDirectory(root)) continue;
            theme.roots.push_back(root);
            if (indexPath.empty() && FileExists(root + "/index.theme")) indexPath = root + "/index.theme";
        }
        if (indexPath.empty()) return theme;
        auto ini = ReadIni(indexPath);
        auto& main = ini["Icon Theme"];
        theme.inherits = SplitList(main["Inherits"]);
        std::vector<std::string> subdirs = SplitList(main["Directories"]);
        for (const auto& extra : SplitList(main["ScaledDirectories"])) subdirs.push_back(extra);
        for (const auto& sub : subdirs) {
            auto g = ini.find(sub);
            if (g == ini.end()) continue;
            ThemeDir d;
            d.subdir = sub;
            auto get = [&](const char* k, int def) {
                auto kv = g->second.find(k);
                return (kv == g->second.end()) ? def : atoi(kv->second.c_str());
            };
            d.size = get("Size", 48);
            d.scale = get("Scale", 1);
            d.minSize = get("MinSize", d.size);
            d.maxSize = get("MaxSize", d.size);
            d.threshold = get("Threshold", 2);
            auto t = g->second.find("Type");
            std::string type = (t == g->second.end()) ? "Threshold" : t->second;
            if (type == "Fixed") d.type = ThemeDir::kFixed;
            else if (type == "Scalable") d.type = ThemeDir::kScalable;
            else d.type = ThemeDir::kThreshold;
            theme.dirs.push_back(d);
        }
        theme.valid = true;
        return theme;
    }

    static int SizeDistance(const ThemeDir& d, int size) {
        int dirSize = d.size * d.scale;
        switch (d.type) {
            case ThemeDir::kFixed:
                return std::abs(dirSize - size);
            case ThemeDir::kScalable:
                if (size < d.minSize * d.scale) return d.minSize * d.scale - size;
                if (size > d.maxSize * d.scale) return size - d.maxSize * d.scale;
                return 0;
            case ThemeDir::kThreshold:
            default:
                if (size < (d.size - d.threshold) * d.scale) return (d.size - d.threshold) * d.scale - size;
                if (size > (d.size + d.threshold) * d.scale) return size - (d.size + d.threshold) * d.scale;
                return 0;
        }
    }

    std::string FindInTheme(const std::string& themeName, const std::string& icon, int size) {
        Theme& theme = GetTheme(themeName);
        if (!theme.valid) return std::string();
        std::string best;
        int bestDistance = 1 << 30;
        bool bestIsSvg = false;
        for (const auto& d : theme.dirs) {
            int distance = SizeDistance(d, size);
            // Only look at a directory if it could beat what we already have;
            // a bitmap that has to be scaled *up* is penalized so a scalable
            // SVG wins over a blurry small PNG.
            if (distance > bestDistance) continue;
            for (const auto& root : theme.roots) {
                for (const char* ext : {".svg", ".png", ".xpm"}) {
                    std::string path = root + "/" + d.subdir + "/" + icon + ext;
                    if (!FileExists(path)) continue;
                    bool isSvg = (ext[1] == 's');
                    int penalized = distance;
                    if (!isSvg && d.type != ThemeDir::kScalable && d.size * d.scale < size) penalized += 1;
                    if (penalized < bestDistance || (penalized == bestDistance && isSvg && !bestIsSvg)) {
                        best = path;
                        bestDistance = penalized;
                        bestIsSvg = isSvg;
                    }
                    break;
                }
            }
            if (bestDistance == 0 && bestIsSvg) return best;
        }
        return best;
    }

    std::string FindInThemes(const std::string& icon, int size) {
        std::vector<std::string> order;
        std::set<std::string> seen;
        std::function<void(const std::string&)> walk = [&](const std::string& name) {
            if (seen.count(name)) return;
            seen.insert(name);
            order.push_back(name);
            for (const auto& parent : GetTheme(name).inherits) walk(parent);
        };
        walk(fThemeName);
        // Breeze and Adwaita hold most generic names the others inherit from.
        for (const char* extra : {"breeze", "Adwaita", "hicolor"}) walk(extra);
        for (const auto& name : order) {
            std::string found = FindInTheme(name, icon, size);
            if (!found.empty()) return found;
        }
        // Many themes now ship status/panel icons only as "-symbolic".
        if (!g_str_has_suffix(icon.c_str(), "-symbolic")) {
            for (const auto& name : order) {
                std::string found = FindInTheme(name, icon + "-symbolic", size);
                if (!found.empty()) return found;
            }
        }
        // Last resort: any other installed theme that happens to have it
        // (tray apps often assume the icon exists everywhere).
        for (const auto& base : fBaseDirs) {
            DIR* d = opendir(base.c_str());
            if (d == nullptr) continue;
            std::vector<std::string> themes;
            while (struct dirent* e = readdir(d)) {
                if (e->d_name[0] != '.' && !seen.count(e->d_name)) themes.push_back(e->d_name);
            }
            closedir(d);
            for (const auto& name : themes) {
                seen.insert(name);
                std::string found = FindInTheme(name, icon, size);
                if (!found.empty()) return found;
            }
        }
        return std::string();
    }

    static std::string FindInFlatDir(const std::string& dir, const std::string& icon, bool recurseOneLevel) {
        for (const char* ext : {".svg", ".png", ".xpm"}) {
            std::string p = dir + "/" + icon + ext;
            if (FileExists(p)) return p;
        }
        if (recurseOneLevel) {
            // IconThemePath usually points at a whole mini theme (hicolor/NNxNN/apps).
            DIR* d = opendir(dir.c_str());
            if (d == nullptr) return std::string();
            std::string found;
            std::vector<std::string> subdirs;
            while (struct dirent* e = readdir(d)) {
                if (e->d_name[0] == '.') continue;
                subdirs.push_back(dir + "/" + e->d_name);
            }
            closedir(d);
            for (const auto& sub : subdirs) {
                for (const char* mid : {"", "/apps", "/status", "/panel"}) {
                    for (const char* ext : {".svg", ".png"}) {
                        std::string p = sub + mid + "/" + icon + ext;
                        if (FileExists(p)) return p;
                    }
                }
                DIR* d2 = opendir(sub.c_str());
                if (d2 == nullptr) continue;
                std::vector<std::string> subsub;
                while (struct dirent* e = readdir(d2)) {
                    if (e->d_name[0] == '.') continue;
                    subsub.push_back(sub + "/" + e->d_name);
                }
                closedir(d2);
                for (const auto& ss : subsub) {
                    for (const char* mid : {"", "/apps", "/status", "/panel"}) {
                        for (const char* ext : {".svg", ".png"}) {
                            std::string p = ss + mid + "/" + icon + ext;
                            if (FileExists(p)) return p;
                        }
                    }
                }
            }
        }
        return std::string();
    }

    std::vector<std::string> fBaseDirs;
    std::vector<std::string> fPixmapDirs;
    std::string fThemeName;
    std::map<std::string, Theme> fThemes;
    std::unordered_map<std::string, std::string> fCache;
};

bool IconTheme::LoadWithPixbuf(cairo_t* cr, const std::string& path, int pxSize) {
    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), pxSize, pxSize, TRUE, &err);
    if (pb == nullptr) {
        g_clear_error(&err);
        return false;
    }
    int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
    int stride = gdk_pixbuf_get_rowstride(pb), n = gdk_pixbuf_get_n_channels(pb);
    bool alpha = gdk_pixbuf_get_has_alpha(pb);
    const guchar* px = gdk_pixbuf_get_pixels(pb);
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    unsigned char* dst = cairo_image_surface_get_data(img);
    int dstStride = cairo_image_surface_get_stride(img);
    for (int y = 0; y < h; ++y) {
        const guchar* s = px + y * stride;
        uint32_t* d = reinterpret_cast<uint32_t*>(dst + y * dstStride);
        for (int x = 0; x < w; ++x) {
            uint32_t r = s[0], g = s[1], b = s[2], a = alpha ? s[3] : 255;
            r = r * a / 255; g = g * a / 255; b = b * a / 255;
            d[x] = (a << 24) | (r << 16) | (g << 8) | b;
            s += n;
        }
    }
    cairo_surface_mark_dirty(img);
    PaintScaled(cr, img, pxSize);
    cairo_surface_destroy(img);
    g_object_unref(pb);
    return true;
}

static IconTheme* gIconTheme = nullptr;

// Draws a generic "application" placeholder so an app without any icon still
// gets a visible, clickable dock tile.
static cairo_surface_t* MakePlaceholderIcon(int pxSize, const std::string& label) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pxSize, pxSize);
    cairo_t* cr = cairo_create(s);
    double m = pxSize * 0.08;
    RoundedRectPath(cr, m, m, pxSize - 2 * m, pxSize - 2 * m, pxSize * 0.18);
    cairo_pattern_t* grad = cairo_pattern_create_linear(0, 0, 0, pxSize);
    cairo_pattern_add_color_stop_rgb(grad, 0, 0.45, 0.50, 0.62);
    cairo_pattern_add_color_stop_rgb(grad, 1, 0.24, 0.27, 0.36);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    std::string letter = label.empty() ? "?" : label.substr(0, 1);
    for (auto& c : letter) c = static_cast<char>(g_ascii_toupper(c));
    double tw = 0, th = 0;
    MeasureText(letter, pxSize * 0.5, true, &tw, &th);
    DrawText(cr, letter, (pxSize - tw) / 2.0, (pxSize - th) / 2.0, pxSize * 0.5, true, RGBA{1, 1, 1, 0.95});
    cairo_destroy(cr);
    cairo_surface_flush(s);
    return s;
}

// =========================================================================
// DESKTOP APPLICATIONS (.desktop files)
// =========================================================================
// Replaces be_roster / app signatures. Wayland tells us each window's app_id;
// this maps that back to a .desktop file for the icon, name and launch
// command, trying the same heuristics other docks use because app_ids in the
// wild are inconsistent ("firefox" vs "org.mozilla.firefox", "Code" vs
// "code-oss", StartupWMClass, ...).
struct AppEntry {
    std::string id;          // desktop file id ("org.kde.dolphin.desktop")
    std::string name;
    std::string iconName;    // themed name or absolute path
    std::string path;        // .desktop file path
    std::string executable;
    std::string wmClass;
    bool show = true;        // NoDisplay/Hidden/OnlyShowIn aware
    bool isFileManager = false;
    GDesktopAppInfo* info = nullptr;
};

class AppDatabase {
public:
    AppDatabase() { Reload(); }
    ~AppDatabase() { Clear(); }

    void Reload() {
        Clear();
        GList* all = g_app_info_get_all();
        for (GList* l = all; l != nullptr; l = l->next) {
            GAppInfo* ai = G_APP_INFO(l->data);
            if (!G_IS_DESKTOP_APP_INFO(ai)) continue;
            GDesktopAppInfo* dai = G_DESKTOP_APP_INFO(ai);
            auto entry = std::make_unique<AppEntry>();
            entry->info = G_DESKTOP_APP_INFO(g_object_ref(dai));
            const char* id = g_app_info_get_id(ai);
            entry->id = id ? id : "";
            const char* name = g_app_info_get_name(ai);
            entry->name = name ? name : entry->id;
            const char* fn = g_desktop_app_info_get_filename(dai);
            entry->path = fn ? fn : "";
            const char* exe = g_app_info_get_executable(ai);
            entry->executable = exe ? exe : "";
            const char* wm = g_desktop_app_info_get_startup_wm_class(dai);
            entry->wmClass = wm ? wm : "";
            entry->show = g_app_info_should_show(ai);
            const char* cats = g_desktop_app_info_get_categories(dai);
            entry->isFileManager = cats && strstr(cats, "FileManager") != nullptr;
            entry->iconName = IconNameFor(g_app_info_get_icon(ai));
            fById[ToLower(entry->id)] = entry.get();
            fEntries.push_back(std::move(entry));
        }
        g_list_free_full(all, g_object_unref);
        fResolveCache.clear();
    }

    static std::string IconNameFor(GIcon* icon) {
        if (icon == nullptr) return std::string();
        if (G_IS_THEMED_ICON(icon)) {
            const char* const* names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            if (names && names[0]) return names[0];
        } else if (G_IS_FILE_ICON(icon)) {
            GFile* f = g_file_icon_get_file(G_FILE_ICON(icon));
            char* p = g_file_get_path(f);
            std::string r = p ? p : "";
            g_free(p);
            return r;
        }
        return std::string();
    }

    const std::vector<std::unique_ptr<AppEntry>>& Entries() const { return fEntries; }

    AppEntry* ById(const std::string& id) {
        auto it = fById.find(ToLower(id));
        return it == fById.end() ? nullptr : it->second;
    }

    // app_id (Wayland) -> desktop entry, or nullptr.
    AppEntry* Resolve(const std::string& appId) {
        if (appId.empty()) return nullptr;
        auto cached = fResolveCache.find(appId);
        if (cached != fResolveCache.end()) return cached->second;
        AppEntry* found = ResolveUncached(appId);
        fResolveCache[appId] = found;
        return found;
    }

    // A process's executable -> desktop entry (tray/CPU menu icons).
    AppEntry* ResolveExecutable(const std::string& exeBase) {
        std::string lower = ToLower(exeBase);
        for (const auto& e : fEntries) {
            if (!e->executable.empty() && ToLower(Basename(e->executable)) == lower) return e.get();
        }
        return Resolve(exeBase);
    }

    static std::string Basename(const std::string& p) {
        size_t slash = p.rfind('/');
        return slash == std::string::npos ? p : p.substr(slash + 1);
    }

private:
    AppEntry* ResolveUncached(const std::string& appId) {
        std::string lower = ToLower(appId);
        // 1. exact desktop id, as the xdg-shell spec intends
        if (AppEntry* e = ById(lower + ".desktop")) return e;
        // 2. StartupWMClass
        for (const auto& e : fEntries) {
            if (!e->wmClass.empty() && ToLower(e->wmClass) == lower) return e.get();
        }
        // 3. last reverse-DNS component of the desktop id ("org.mozilla.firefox" <-> "firefox")
        std::string lastComponent = lower;
        size_t dot = lastComponent.rfind('.');
        if (dot != std::string::npos) lastComponent = lastComponent.substr(dot + 1);
        for (const auto& e : fEntries) {
            std::string id = ToLower(e->id);
            if (g_str_has_suffix(id.c_str(), ".desktop")) id.resize(id.size() - 8);
            std::string idLast = id;
            size_t d = idLast.rfind('.');
            if (d != std::string::npos) idLast = idLast.substr(d + 1);
            if (id == lower || idLast == lower || idLast == lastComponent) return e.get();
        }
        // 4. executable name
        for (const auto& e : fEntries) {
            if (!e->executable.empty()) {
                std::string exe = ToLower(Basename(e->executable));
                if (exe == lower || exe == lastComponent) return e.get();
            }
        }
        // 5. display name
        for (const auto& e : fEntries) {
            if (ToLower(e->name) == lower) return e.get();
        }
        // 6. GIO's own fuzzy search as a last resort
        gchar*** results = g_desktop_app_info_search(appId.c_str());
        AppEntry* best = nullptr;
        if (results != nullptr) {
            if (results[0] && results[0][0]) best = ById(results[0][0]);
            for (gchar*** r = results; *r; ++r) g_strfreev(*r);
            g_free(results);
        }
        return best;
    }

    void Clear() {
        for (auto& e : fEntries) {
            if (e->info) g_object_unref(e->info);
        }
        fEntries.clear();
        fById.clear();
        fResolveCache.clear();
    }

    std::vector<std::unique_ptr<AppEntry>> fEntries;
    std::unordered_map<std::string, AppEntry*> fById;
    std::unordered_map<std::string, AppEntry*> fResolveCache;
};

static AppDatabase* gApps = nullptr;

// =========================================================================
// WAYLAND CORE
// =========================================================================
class Surface;

struct Output {
    wl_output* output = nullptr;
    uint32_t globalName = 0;
    std::string name;
    int scale = 1;
    int width = 0, height = 0;
};

struct WaylandState {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    uint32_t compositorVersion = 0;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xdg_wm_base* wmBase = nullptr;
    zwlr_layer_shell_v1* layerShell = nullptr;
    uint32_t layerShellVersion = 0;
    zwlr_foreign_toplevel_manager_v1* wlrToplevels = nullptr;
    org_kde_plasma_window_management* plasmaWindows = nullptr;
    org_kde_plasma_virtual_desktop_management* plasmaDesktops = nullptr;
    ext_workspace_manager_v1* extWorkspaces = nullptr;
    xdg_activation_v1* activation = nullptr;
    wp_fractional_scale_manager_v1* fractionalScale = nullptr;
    wp_viewporter* viewporter = nullptr;
    std::vector<std::unique_ptr<Output>> outputs;

    wl_cursor_theme* cursorTheme = nullptr;
    int cursorThemeScale = 0;
    wl_surface* cursorSurface = nullptr;

    uint32_t lastInputSerial = 0;     // last button/key press: for popup grabs and activation tokens
    uint32_t pointerEnterSerial = 0;
    Surface* pointerFocus = nullptr;
    Surface* keyboardFocus = nullptr;
    double pointerX = 0, pointerY = 0;

    xkb_context* xkb = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkbState = nullptr;
    int32_t repeatRate = 25, repeatDelay = 600;
    guint repeatTimer = 0;
    uint32_t repeatKey = 0;
};

static WaylandState gWl;
static GMainLoop* gMainLoop = nullptr;

static void QuitApplication() {
    if (gMainLoop) g_main_loop_quit(gMainLoop);
}

// ---- Input target interface ----------------------------------------------
// Every surface we create (dock, popups, panels) registers itself as the
// wl_surface's user data so seat events can be routed straight to it.
class Surface {
public:
    virtual ~Surface() { DestroySurface(); }

    wl_surface* surface = nullptr;
    wp_fractional_scale_v1* fractional = nullptr;
    wp_viewport* viewport = nullptr;
    double scale = 1.0;          // effective device-pixel ratio
    int intScale = 1;            // integer fallback when fractional scaling isn't available
    int preferredIntScale = 0;   // from wl_surface.preferred_buffer_scale (v6)
    std::set<Output*> enteredOutputs;

    virtual void PointerEnter(double x, double y) {}
    virtual void PointerMotion(double x, double y) {}
    virtual void PointerLeave() {}
    virtual void PointerButton(int button, bool pressed, uint32_t serial) {}
    virtual void PointerAxis(double dy) {}
    virtual void KeyboardEnter() {}
    virtual void KeyboardLeave() {}
    virtual void Key(xkb_keysym_t sym, const std::string& utf8) {}
    virtual void ScaleChanged() {}

    bool UsesFractional() const { return fractional != nullptr && viewport != nullptr; }

    void CreateSurface() {
        surface = wl_compositor_create_surface(gWl.compositor);
        wl_surface_set_user_data(surface, this);
        wl_surface_add_listener(surface, &kSurfaceListener, this);
        if (gWl.fractionalScale && gWl.viewporter) {
            fractional = wp_fractional_scale_manager_v1_get_fractional_scale(gWl.fractionalScale, surface);
            wp_fractional_scale_v1_add_listener(fractional, &kFractionalListener, this);
            viewport = wp_viewporter_get_viewport(gWl.viewporter, surface);
        }
        // Until the compositor tells us, guess from the largest output.
        int guess = 1;
        for (const auto& o : gWl.outputs) guess = std::max(guess, o->scale);
        intScale = guess;
        scale = guess;
    }

    void DestroySurface() {
        if (gWl.pointerFocus == this) gWl.pointerFocus = nullptr;
        if (gWl.keyboardFocus == this) gWl.keyboardFocus = nullptr;
        if (viewport) { wp_viewport_destroy(viewport); viewport = nullptr; }
        if (fractional) { wp_fractional_scale_v1_destroy(fractional); fractional = nullptr; }
        if (surface) { wl_surface_destroy(surface); surface = nullptr; }
    }

private:
    void RecomputeIntScale() {
        if (UsesFractional()) return;
        int s = preferredIntScale;
        if (s <= 0) {
            s = 1;
            for (Output* o : enteredOutputs) s = std::max(s, o->scale);
        }
        if (s != intScale) {
            intScale = s;
            scale = s;
            ScaleChanged();
        }
    }

    static const wl_surface_listener kSurfaceListener;
    static const wp_fractional_scale_v1_listener kFractionalListener;
};

const wl_surface_listener Surface::kSurfaceListener = {
    .enter = [](void* data, wl_surface*, wl_output* output) {
        auto* self = static_cast<Surface*>(data);
        for (const auto& o : gWl.outputs) {
            if (o->output == output) self->enteredOutputs.insert(o.get());
        }
        self->RecomputeIntScale();
    },
    .leave = [](void* data, wl_surface*, wl_output* output) {
        auto* self = static_cast<Surface*>(data);
        for (const auto& o : gWl.outputs) {
            if (o->output == output) self->enteredOutputs.erase(o.get());
        }
        self->RecomputeIntScale();
    },
    .preferred_buffer_scale = [](void* data, wl_surface*, int32_t factor) {
        auto* self = static_cast<Surface*>(data);
        self->preferredIntScale = factor;
        self->RecomputeIntScale();
    },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

const wp_fractional_scale_v1_listener Surface::kFractionalListener = {
    .preferred_scale = [](void* data, wp_fractional_scale_v1*, uint32_t scale120) {
        auto* self = static_cast<Surface*>(data);
        double s = scale120 / 120.0;
        if (std::abs(s - self->scale) > 0.001) {
            self->scale = s;
            self->ScaleChanged();
        }
    },
};

static Surface* SurfaceFromWl(wl_surface* s) {
    if (s == nullptr) return nullptr;
    // Our cursor surface has no user data.
    return static_cast<Surface*>(wl_surface_get_user_data(s));
}

// ---- Cursor ---------------------------------------------------------------
static void SetDefaultCursor(uint32_t serial) {
    if (gWl.pointer == nullptr || gWl.shm == nullptr) return;
    int scale = 1;
    for (const auto& o : gWl.outputs) scale = std::max(scale, o->scale);
    if (gWl.cursorTheme == nullptr || gWl.cursorThemeScale != scale) {
        if (gWl.cursorTheme) wl_cursor_theme_destroy(gWl.cursorTheme);
        const char* themeName = g_getenv("XCURSOR_THEME");
        const char* sizeStr = g_getenv("XCURSOR_SIZE");
        int size = sizeStr ? atoi(sizeStr) : 24;
        if (size <= 0) size = 24;
        gWl.cursorTheme = wl_cursor_theme_load(themeName, size * scale, gWl.shm);
        gWl.cursorThemeScale = scale;
    }
    if (gWl.cursorTheme == nullptr) return;
    wl_cursor* cursor = wl_cursor_theme_get_cursor(gWl.cursorTheme, "left_ptr");
    if (cursor == nullptr) cursor = wl_cursor_theme_get_cursor(gWl.cursorTheme, "default");
    if (cursor == nullptr || cursor->image_count == 0) return;
    wl_cursor_image* image = cursor->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (gWl.cursorSurface == nullptr) gWl.cursorSurface = wl_compositor_create_surface(gWl.compositor);
    wl_surface_set_buffer_scale(gWl.cursorSurface, scale);
    wl_surface_attach(gWl.cursorSurface, buffer, 0, 0);
    wl_surface_damage(gWl.cursorSurface, 0, 0, image->width, image->height);
    wl_surface_commit(gWl.cursorSurface);
    wl_pointer_set_cursor(gWl.pointer, serial, gWl.cursorSurface,
        image->hotspot_x / scale, image->hotspot_y / scale);
}

// ---- Pointer ----------------------------------------------------------------
static double gAxisAccum = 0.0;
static bool gAxisDiscreteSeen = false;

static const wl_pointer_listener kPointerListener = {
    .enter = [](void*, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
        gWl.pointerEnterSerial = serial;
        SetDefaultCursor(serial);
        Surface* target = SurfaceFromWl(s);
        gWl.pointerFocus = target;
        gWl.pointerX = wl_fixed_to_double(x);
        gWl.pointerY = wl_fixed_to_double(y);
        if (target) target->PointerEnter(gWl.pointerX, gWl.pointerY);
    },
    .leave = [](void*, wl_pointer*, uint32_t, wl_surface* s) {
        Surface* target = SurfaceFromWl(s);
        if (gWl.pointerFocus == target) gWl.pointerFocus = nullptr;
        if (target) target->PointerLeave();
    },
    .motion = [](void*, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        gWl.pointerX = wl_fixed_to_double(x);
        gWl.pointerY = wl_fixed_to_double(y);
        if (gWl.pointerFocus) gWl.pointerFocus->PointerMotion(gWl.pointerX, gWl.pointerY);
    },
    .button = [](void*, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
        bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
        if (pressed) gWl.lastInputSerial = serial;
        int b = kButtonNone;
        if (button == BTN_LEFT) b = kButtonLeft;
        else if (button == BTN_MIDDLE) b = kButtonMiddle;
        else if (button == BTN_RIGHT) b = kButtonRight;
        if (b != kButtonNone && gWl.pointerFocus) gWl.pointerFocus->PointerButton(b, pressed, serial);
    },
    .axis = [](void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) gAxisAccum += wl_fixed_to_double(value);
    },
    .frame = [](void*, wl_pointer*) {
        // One "notch" is 10 units of continuous scroll on every compositor we
        // care about; touchpads accumulate until a notch's worth has built up.
        if (gAxisAccum != 0.0 && gWl.pointerFocus) {
            double notches = gAxisAccum / 10.0;
            if (std::abs(notches) >= 1.0 || gAxisDiscreteSeen) {
                // SDL/Haiku convention: positive = wheel up (away from user).
                double dy = -std::round(notches);
                if (dy == 0.0) dy = (notches < 0) ? 1.0 : -1.0;
                gWl.pointerFocus->PointerAxis(dy);
                gAxisAccum = 0.0;
            }
        } else if (gAxisAccum != 0.0) {
            gAxisAccum = 0.0;
        }
        gAxisDiscreteSeen = false;
    },
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) { gAxisAccum = 0.0; },
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) { gAxisDiscreteSeen = true; },
    .axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) { gAxisDiscreteSeen = true; },
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {},
};

// ---- Keyboard ---------------------------------------------------------------
static void DeliverKey(uint32_t key) {
    if (gWl.xkbState == nullptr || gWl.keyboardFocus == nullptr) return;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(gWl.xkbState, key + 8);
    char buf[64] = {0};
    xkb_state_key_get_utf8(gWl.xkbState, key + 8, buf, sizeof(buf));
    gWl.keyboardFocus->Key(sym, buf);
}

static void StopKeyRepeat() {
    if (gWl.repeatTimer) {
        g_source_remove(gWl.repeatTimer);
        gWl.repeatTimer = 0;
    }
}

static const wl_keyboard_listener kKeyboardListener = {
    .keymap = [](void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
        char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);
        if (map == MAP_FAILED) return;
        if (gWl.xkb == nullptr) gWl.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap* keymap = xkb_keymap_new_from_string(gWl.xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);
        if (keymap == nullptr) return;
        if (gWl.xkbState) xkb_state_unref(gWl.xkbState);
        if (gWl.keymap) xkb_keymap_unref(gWl.keymap);
        gWl.keymap = keymap;
        gWl.xkbState = xkb_state_new(keymap);
    },
    .enter = [](void*, wl_keyboard*, uint32_t serial, wl_surface* s, wl_array*) {
        gWl.keyboardFocus = SurfaceFromWl(s);
        if (gWl.keyboardFocus) gWl.keyboardFocus->KeyboardEnter();
    },
    .leave = [](void*, wl_keyboard*, uint32_t, wl_surface* s) {
        StopKeyRepeat();
        Surface* target = SurfaceFromWl(s);
        if (gWl.keyboardFocus == target) gWl.keyboardFocus = nullptr;
        if (target) target->KeyboardLeave();
    },
    .key = [](void*, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            gWl.lastInputSerial = serial;
            DeliverKey(key);
            StopKeyRepeat();
            if (gWl.keymap && xkb_keymap_key_repeats(gWl.keymap, key + 8) && gWl.repeatRate > 0) {
                gWl.repeatKey = key;
                gWl.repeatTimer = g_timeout_add(gWl.repeatDelay, [](gpointer) -> gboolean {
                    DeliverKey(gWl.repeatKey);
                    gWl.repeatTimer = g_timeout_add(1000 / std::max(1, gWl.repeatRate), [](gpointer) -> gboolean {
                        DeliverKey(gWl.repeatKey);
                        return G_SOURCE_CONTINUE;
                    }, nullptr);
                    return G_SOURCE_REMOVE;
                }, nullptr);
            }
        } else if (key == gWl.repeatKey) {
            StopKeyRepeat();
        }
    },
    .modifiers = [](void*, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
        if (gWl.xkbState) xkb_state_update_mask(gWl.xkbState, depressed, latched, locked, 0, 0, group);
    },
    .repeat_info = [](void*, wl_keyboard*, int32_t rate, int32_t delay) {
        gWl.repeatRate = rate;
        gWl.repeatDelay = delay;
    },
};

static const wl_seat_listener kSeatListener = {
    .capabilities = [](void*, wl_seat* seat, uint32_t caps) {
        bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
        bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
        if (hasPointer && gWl.pointer == nullptr) {
            gWl.pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(gWl.pointer, &kPointerListener, nullptr);
        } else if (!hasPointer && gWl.pointer) {
            wl_pointer_release(gWl.pointer);
            gWl.pointer = nullptr;
        }
        if (hasKeyboard && gWl.keyboard == nullptr) {
            gWl.keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(gWl.keyboard, &kKeyboardListener, nullptr);
        } else if (!hasKeyboard && gWl.keyboard) {
            wl_keyboard_release(gWl.keyboard);
            gWl.keyboard = nullptr;
        }
    },
    .name = [](void*, wl_seat*, const char*) {},
};

// ---- Outputs ----------------------------------------------------------------
static std::function<void()> gOnOutputsChanged;

static const wl_output_listener kOutputListener = {
    .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    .mode = [](void* data, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t) {
        auto* o = static_cast<Output*>(data);
        if (flags & WL_OUTPUT_MODE_CURRENT) { o->width = w; o->height = h; }
    },
    .done = [](void*, wl_output*) {
        if (gOnOutputsChanged) gOnOutputsChanged();
    },
    .scale = [](void* data, wl_output*, int32_t factor) {
        static_cast<Output*>(data)->scale = factor;
    },
    .name = [](void* data, wl_output*, const char* name) {
        static_cast<Output*>(data)->name = name ? name : "";
    },
    .description = [](void*, wl_output*, const char*) {},
};

static const xdg_wm_base_listener kWmBaseListener = {
    .ping = [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
};

static const wl_registry_listener kRegistryListener = {
    .global = [](void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t version) {
        auto bind = [&](const wl_interface* wi, uint32_t maxVersion) {
            return wl_registry_bind(reg, name, wi, std::min(version, maxVersion));
        };
        if (strcmp(iface, wl_compositor_interface.name) == 0) {
            gWl.compositorVersion = std::min(version, 6u);
            gWl.compositor = static_cast<wl_compositor*>(bind(&wl_compositor_interface, 6));
        } else if (strcmp(iface, wl_shm_interface.name) == 0) {
            gWl.shm = static_cast<wl_shm*>(bind(&wl_shm_interface, 1));
        } else if (strcmp(iface, wl_seat_interface.name) == 0 && gWl.seat == nullptr) {
            gWl.seat = static_cast<wl_seat*>(bind(&wl_seat_interface, 5));
            wl_seat_add_listener(gWl.seat, &kSeatListener, nullptr);
        } else if (strcmp(iface, wl_output_interface.name) == 0) {
            auto o = std::make_unique<Output>();
            o->globalName = name;
            o->output = static_cast<wl_output*>(bind(&wl_output_interface, 4));
            wl_output_add_listener(o->output, &kOutputListener, o.get());
            gWl.outputs.push_back(std::move(o));
        } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
            gWl.wmBase = static_cast<xdg_wm_base*>(bind(&xdg_wm_base_interface, 3));
            xdg_wm_base_add_listener(gWl.wmBase, &kWmBaseListener, nullptr);
        } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
            gWl.layerShellVersion = std::min(version, 4u);
            gWl.layerShell = static_cast<zwlr_layer_shell_v1*>(bind(&zwlr_layer_shell_v1_interface, 4));
        } else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
            gWl.wlrToplevels = static_cast<zwlr_foreign_toplevel_manager_v1*>(
                bind(&zwlr_foreign_toplevel_manager_v1_interface, 3));
        } else if (strcmp(iface, org_kde_plasma_window_management_interface.name) == 0 && version >= 17) {
            // The protocol requires binding at least version 17.
            gWl.plasmaWindows = static_cast<org_kde_plasma_window_management*>(
                bind(&org_kde_plasma_window_management_interface, 16 + 5));
        } else if (strcmp(iface, org_kde_plasma_virtual_desktop_management_interface.name) == 0) {
            gWl.plasmaDesktops = static_cast<org_kde_plasma_virtual_desktop_management*>(
                bind(&org_kde_plasma_virtual_desktop_management_interface, 4));
        } else if (strcmp(iface, ext_workspace_manager_v1_interface.name) == 0) {
            gWl.extWorkspaces = static_cast<ext_workspace_manager_v1*>(bind(&ext_workspace_manager_v1_interface, 1));
        } else if (strcmp(iface, xdg_activation_v1_interface.name) == 0) {
            gWl.activation = static_cast<xdg_activation_v1*>(bind(&xdg_activation_v1_interface, 1));
        } else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
            gWl.fractionalScale = static_cast<wp_fractional_scale_manager_v1*>(
                bind(&wp_fractional_scale_manager_v1_interface, 1));
        } else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
            gWl.viewporter = static_cast<wp_viewporter*>(bind(&wp_viewporter_interface, 1));
        }
    },
    .global_remove = [](void*, wl_registry*, uint32_t name) {
        for (auto it = gWl.outputs.begin(); it != gWl.outputs.end(); ++it) {
            if ((*it)->globalName == name) {
                wl_output_release((*it)->output);
                gWl.outputs.erase(it);
                if (gOnOutputsChanged) gOnOutputsChanged();
                return;
            }
        }
    },
};

static Output* FindOutput(const std::string& name) {
    if (name.empty()) return nullptr;
    for (const auto& o : gWl.outputs) {
        if (o->name == name) return o.get();
    }
    return nullptr;
}

// ---- GLib main loop integration ----------------------------------------------
// The whole dock runs on one GLib main loop (D-Bus for the tray, PulseAudio,
// timers), with the Wayland socket plugged in as a GSource using the standard
// prepare_read / read_events handshake.
struct WaylandSource {
    GSource base;
    gpointer tag;
    bool reading;
};

static GSourceFuncs kWaylandSourceFuncs = {
    .prepare = [](GSource* s, gint* timeout) -> gboolean {
        auto* ws = reinterpret_cast<WaylandSource*>(s);
        *timeout = -1;
        if (ws->reading) return FALSE;
        while (wl_display_prepare_read(gWl.display) != 0) {
            if (wl_display_dispatch_pending(gWl.display) < 0) return TRUE;
        }
        wl_display_flush(gWl.display);
        ws->reading = true;
        return FALSE;
    },
    .check = [](GSource* s) -> gboolean {
        auto* ws = reinterpret_cast<WaylandSource*>(s);
        GIOCondition cond = g_source_query_unix_fd(s, ws->tag);
        if (ws->reading) {
            if (cond & G_IO_IN) {
                if (wl_display_read_events(gWl.display) < 0) {
                    ws->reading = false;
                    return TRUE;
                }
            } else {
                wl_display_cancel_read(gWl.display);
            }
            ws->reading = false;
        }
        return (cond & (G_IO_IN | G_IO_ERR | G_IO_HUP)) != 0;
    },
    .dispatch = [](GSource* s, GSourceFunc, gpointer) -> gboolean {
        auto* ws = reinterpret_cast<WaylandSource*>(s);
        GIOCondition cond = g_source_query_unix_fd(s, ws->tag);
        if (cond & (G_IO_ERR | G_IO_HUP)) {
            WarnLog("lost the Wayland connection\n");
            QuitApplication();
            return G_SOURCE_REMOVE;
        }
        if (wl_display_dispatch_pending(gWl.display) < 0) {
            WarnLog("Wayland protocol error: %s\n", strerror(wl_display_get_error(gWl.display)));
            QuitApplication();
            return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    },
    .finalize = nullptr,
    .closure_callback = nullptr,
    .closure_marshal = nullptr,
};

static void AttachWaylandSource() {
    GSource* s = g_source_new(&kWaylandSourceFuncs, sizeof(WaylandSource));
    auto* ws = reinterpret_cast<WaylandSource*>(s);
    ws->reading = false;
    ws->tag = g_source_add_unix_fd(s, wl_display_get_fd(gWl.display),
        static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP));
    g_source_set_priority(s, G_PRIORITY_HIGH);
    g_source_attach(s, nullptr);
    g_source_unref(s);
}

// Defers a callback to the next main loop iteration -- used whenever a click
// handler wants to destroy the very surface that delivered the click.
static void RunLater(std::function<void()> fn) {
    auto* heap = new std::function<void()>(std::move(fn));
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer p) -> gboolean {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
    }, heap, nullptr);
}

static guint RunAfter(unsigned ms, std::function<void()> fn) {
    auto* heap = new std::function<void()>(std::move(fn));
    return g_timeout_add_full(G_PRIORITY_DEFAULT, ms, [](gpointer p) -> gboolean {
        (*static_cast<std::function<void()>*>(p))();
        return G_SOURCE_REMOVE;
    }, heap, [](gpointer p) { delete static_cast<std::function<void()>*>(p); });
}

// ---- xdg-activation tokens ---------------------------------------------------
// KWin (and others with focus-stealing prevention) only let a newly launched
// app take focus if the launcher passes on a token tied to the click that
// launched it. Requests one, then calls back with it (or with an empty string
// if the compositor doesn't support the protocol / doesn't answer in time).
struct ActivationRequest {
    xdg_activation_token_v1* token = nullptr;
    std::function<void(const std::string&)> callback;
    guint timeout = 0;
    bool done = false;
};

static void FinishActivationRequest(ActivationRequest* req, const std::string& token) {
    if (req->done) return;
    req->done = true;
    if (req->timeout) g_source_remove(req->timeout);
    req->timeout = 0;
    auto cb = std::move(req->callback);
    if (req->token) xdg_activation_token_v1_destroy(req->token);
    delete req;
    if (cb) cb(token);
}

static const xdg_activation_token_v1_listener kActivationTokenListener = {
    .done = [](void* data, xdg_activation_token_v1*, const char* token) {
        FinishActivationRequest(static_cast<ActivationRequest*>(data), token ? token : "");
    },
};

static void RequestActivationToken(wl_surface* surface, const std::string& appId,
    std::function<void(const std::string&)> callback) {
    if (gWl.activation == nullptr) {
        callback(std::string());
        return;
    }
    auto* req = new ActivationRequest();
    req->callback = std::move(callback);
    req->token = xdg_activation_v1_get_activation_token(gWl.activation);
    xdg_activation_token_v1_add_listener(req->token, &kActivationTokenListener, req);
    if (gWl.seat && gWl.lastInputSerial) xdg_activation_token_v1_set_serial(req->token, gWl.lastInputSerial, gWl.seat);
    if (surface) xdg_activation_token_v1_set_surface(req->token, surface);
    if (!appId.empty()) xdg_activation_token_v1_set_app_id(req->token, appId.c_str());
    xdg_activation_token_v1_commit(req->token);
    req->timeout = g_timeout_add(500, [](gpointer p) -> gboolean {
        auto* r = static_cast<ActivationRequest*>(p);
        r->timeout = 0;
        FinishActivationRequest(r, std::string());
        return G_SOURCE_REMOVE;
    }, req);
}

// Launches a desktop entry (the be_roster->Launch() equivalent).
static wl_surface* gActivationSurface = nullptr; // the dock's surface, set once it exists

static void LaunchApp(AppEntry* app, const std::vector<std::string>& uris = {}) {
    if (app == nullptr || app->info == nullptr) return;
    GDesktopAppInfo* info = G_DESKTOP_APP_INFO(g_object_ref(app->info));
    std::string appId = app->id;
    if (g_str_has_suffix(appId.c_str(), ".desktop")) appId.resize(appId.size() - 8);
    RequestActivationToken(gActivationSurface, appId, [info, uris](const std::string& token) {
        GAppLaunchContext* ctx = g_app_launch_context_new();
        if (!token.empty()) {
            g_app_launch_context_setenv(ctx, "XDG_ACTIVATION_TOKEN", token.c_str());
            g_app_launch_context_setenv(ctx, "DESKTOP_STARTUP_ID", token.c_str());
        }
        GList* uriList = nullptr;
        for (const auto& u : uris) uriList = g_list_append(uriList, g_strdup(u.c_str()));
        GError* err = nullptr;
        if (!g_app_info_launch_uris(G_APP_INFO(info), uriList, ctx, &err)) {
            WarnLog("launch failed: %s\n", err ? err->message : "?");
            g_clear_error(&err);
        }
        g_list_free_full(uriList, g_free);
        g_object_unref(ctx);
        g_object_unref(info);
    });
}

static void LaunchCommand(const std::string& command) {
    RequestActivationToken(gActivationSurface, std::string(), [command](const std::string& token) {
        RunDetached(command, token);
    });
}

// Opens a folder / URI in the user's file manager (Tracker's job on Haiku).
static void OpenUri(const std::string& uri) {
    GAppInfo* handler = nullptr;
    char* scheme = g_uri_parse_scheme(uri.c_str());
    if (scheme && strcmp(scheme, "file") != 0) {
        handler = g_app_info_get_default_for_uri_scheme(scheme);
    }
    if (handler == nullptr) handler = g_app_info_get_default_for_type("inode/directory", FALSE);
    g_free(scheme);
    if (handler && G_IS_DESKTOP_APP_INFO(handler)) {
        const char* id = g_app_info_get_id(handler);
        AppEntry* entry = id ? gApps->ById(id) : nullptr;
        if (entry) {
            LaunchApp(entry, {uri});
            g_object_unref(handler);
            return;
        }
    }
    if (handler) g_object_unref(handler);
    LaunchCommand("xdg-open " + ShellQuote(uri));
}

static void OpenPath(const std::string& path) {
    char* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (uri) {
        OpenUri(uri);
        g_free(uri);
    }
}

// =========================================================================
// SHARED-MEMORY CAIRO SURFACES (menus, drawer, settings, alerts)
// =========================================================================
static int CreateShmFile(size_t size) {
    int fd = memfd_create("hdesktop-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) return -1;
    while (ftruncate(fd, size) < 0) {
        if (errno != EINTR) { close(fd); return -1; }
    }
    return fd;
}

class CairoSurface : public Surface {
public:
    int width = 0, height = 0;   // logical size
    bool configured = false;

    virtual ~CairoSurface() {
        if (fFrameCallback) wl_callback_destroy(fFrameCallback);
        if (fRedrawIdle) g_source_remove(fRedrawIdle);
        for (auto& b : fBuffers) FreeBuffer(b);
    }

    virtual void Paint(cairo_t* cr) = 0;

    // Coalesces redraw requests into one paint per frame.
    void Redraw() {
        fDirty = true;
        if (!configured || fFrameCallback != nullptr || fRedrawIdle != 0) return;
        fRedrawIdle = g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer p) -> gboolean {
            auto* self = static_cast<CairoSurface*>(p);
            self->fRedrawIdle = 0;
            self->Render();
            return G_SOURCE_REMOVE;
        }, this, nullptr);
    }

    void ScaleChanged() override { Redraw(); }

protected:
    void Render() {
        if (!configured || surface == nullptr || width <= 0 || height <= 0) return;
        fDirty = false;
        int pw, ph;
        if (UsesFractional()) {
            pw = static_cast<int>(std::ceil(width * scale));
            ph = static_cast<int>(std::ceil(height * scale));
        } else {
            pw = width * intScale;
            ph = height * intScale;
        }
        Buffer* buf = AcquireBuffer(pw, ph);
        if (buf == nullptr) return;

        cairo_t* cr = cairo_create(buf->cairo);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_scale(cr, static_cast<double>(pw) / width, static_cast<double>(ph) / height);
        Paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(buf->cairo);

        if (UsesFractional()) {
            wp_viewport_set_destination(viewport, width, height);
            wl_surface_set_buffer_scale(surface, 1);
        } else {
            wl_surface_set_buffer_scale(surface, intScale);
        }
        wl_surface_attach(surface, buf->buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, pw, ph);
        fFrameCallback = wl_surface_frame(surface);
        wl_callback_add_listener(fFrameCallback, &kFrameListener, this);
        buf->busy = true;
        wl_surface_commit(surface);
    }

private:
    struct Buffer {
        wl_buffer* buffer = nullptr;
        void* data = nullptr;
        size_t size = 0;
        int pw = 0, ph = 0;
        bool busy = false;
        cairo_surface_t* cairo = nullptr;
    };

    Buffer* AcquireBuffer(int pw, int ph) {
        Buffer* chosen = nullptr;
        for (auto& b : fBuffers) {
            if (!b.busy) { chosen = &b; break; }
        }
        if (chosen == nullptr) return nullptr; // both in flight; the release will retrigger
        if (chosen->buffer && (chosen->pw != pw || chosen->ph != ph)) FreeBuffer(*chosen);
        if (chosen->buffer == nullptr) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw);
            size_t size = static_cast<size_t>(stride) * ph;
            int fd = CreateShmFile(size);
            if (fd < 0) return nullptr;
            void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (data == MAP_FAILED) { close(fd); return nullptr; }
            wl_shm_pool* pool = wl_shm_create_pool(gWl.shm, fd, size);
            chosen->buffer = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride, WL_SHM_FORMAT_ARGB8888);
            wl_shm_pool_destroy(pool);
            close(fd);
            chosen->data = data;
            chosen->size = size;
            chosen->pw = pw;
            chosen->ph = ph;
            chosen->cairo = cairo_image_surface_create_for_data(static_cast<unsigned char*>(data),
                CAIRO_FORMAT_ARGB32, pw, ph, stride);
            wl_buffer_add_listener(chosen->buffer, &kBufferListener, chosen);
        }
        return chosen;
    }

    static void FreeBuffer(Buffer& b) {
        if (b.cairo) cairo_surface_destroy(b.cairo);
        if (b.buffer) wl_buffer_destroy(b.buffer);
        if (b.data) munmap(b.data, b.size);
        b = Buffer();
    }

    static const wl_buffer_listener kBufferListener;
    static const wl_callback_listener kFrameListener;

    Buffer fBuffers[2];
    wl_callback* fFrameCallback = nullptr;
    guint fRedrawIdle = 0;
    bool fDirty = false;
};

const wl_buffer_listener CairoSurface::kBufferListener = {
    .release = [](void* data, wl_buffer*) { static_cast<Buffer*>(data)->busy = false; },
};

const wl_callback_listener CairoSurface::kFrameListener = {
    .done = [](void* data, wl_callback* cb, uint32_t) {
        auto* self = static_cast<CairoSurface*>(data);
        wl_callback_destroy(cb);
        self->fFrameCallback = nullptr;
        if (self->fDirty) self->Render();
    },
};

// ---- Layer-shell panel (drawer, settings, alerts) ----------------------------
class LayerPanel : public CairoSurface {
public:
    struct Config {
        uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
        uint32_t anchor = 0;
        int width = 0, height = 0;
        int marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
        uint32_t keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
        std::string nameSpace = "hdesktop";
    };

    zwlr_layer_surface_v1* layerSurface = nullptr;
    std::function<void()> onClosed;

    void CreateLayer(const Config& cfg) {
        CreateSurface();
        Output* out = FindOutput(gSettings.output);
        layerSurface = zwlr_layer_shell_v1_get_layer_surface(gWl.layerShell, surface,
            out ? out->output : nullptr, cfg.layer, cfg.nameSpace.c_str());
        zwlr_layer_surface_v1_add_listener(layerSurface, &kLayerListener, this);
        zwlr_layer_surface_v1_set_anchor(layerSurface, cfg.anchor);
        zwlr_layer_surface_v1_set_size(layerSurface, cfg.width, cfg.height);
        zwlr_layer_surface_v1_set_margin(layerSurface, cfg.marginTop, cfg.marginRight, cfg.marginBottom, cfg.marginLeft);
        uint32_t kb = cfg.keyboard;
        if (kb == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND && gWl.layerShellVersion < 4) {
            kb = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
        }
        zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface, kb);
        width = cfg.width;
        height = cfg.height;
        wl_surface_commit(surface);
    }

    ~LayerPanel() override {
        if (layerSurface) zwlr_layer_surface_v1_destroy(layerSurface);
        layerSurface = nullptr;
    }

protected:
    virtual void Configured() {}

private:
    static const zwlr_layer_surface_v1_listener kLayerListener;
};

const zwlr_layer_surface_v1_listener LayerPanel::kLayerListener = {
    .configure = [](void* data, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
        auto* self = static_cast<LayerPanel*>(data);
        zwlr_layer_surface_v1_ack_configure(ls, serial);
        if (w > 0) self->width = static_cast<int>(w);
        if (h > 0) self->height = static_cast<int>(h);
        self->configured = true;
        self->Configured();
        self->Redraw();
    },
    .closed = [](void* data, zwlr_layer_surface_v1*) {
        auto* self = static_cast<LayerPanel*>(data);
        if (self->onClosed) {
            auto cb = self->onClosed;
            RunLater(cb);
        }
    },
};

// ---- xdg_popup (menus, hover window lists) ------------------------------------
struct PopupAnchor {
    // Rectangle on the parent surface the popup attaches to, in the parent's
    // logical coordinates.
    int x = 0, y = 0, w = 1, h = 1;
    uint32_t anchor = XDG_POSITIONER_ANCHOR_TOP;
    uint32_t gravity = XDG_POSITIONER_GRAVITY_TOP;
    uint32_t constraints = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                           XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
                           XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
    int offsetX = 0, offsetY = 0;
};

class PopupSurface : public CairoSurface {
public:
    xdg_surface* xdgSurface = nullptr;
    xdg_popup* popup = nullptr;
    std::function<void()> onDismissed;

    // parentLayer: attach to a layer surface. parentXdg: attach to another popup.
    bool CreatePopup(zwlr_layer_surface_v1* parentLayer, xdg_surface* parentXdg,
        const PopupAnchor& a, int w, int h, uint32_t grabSerial) {
        if (gWl.wmBase == nullptr) return false;
        CreateSurface();
        width = w;
        height = h;
        xdgSurface = xdg_wm_base_get_xdg_surface(gWl.wmBase, surface);
        xdg_surface_add_listener(xdgSurface, &kXdgSurfaceListener, this);
        xdg_positioner* pos = xdg_wm_base_create_positioner(gWl.wmBase);
        xdg_positioner_set_size(pos, w, h);
        xdg_positioner_set_anchor_rect(pos, a.x, a.y, std::max(1, a.w), std::max(1, a.h));
        xdg_positioner_set_anchor(pos, a.anchor);
        xdg_positioner_set_gravity(pos, a.gravity);
        xdg_positioner_set_constraint_adjustment(pos, a.constraints);
        if (a.offsetX || a.offsetY) xdg_positioner_set_offset(pos, a.offsetX, a.offsetY);
        popup = xdg_surface_get_popup(xdgSurface, parentXdg, pos);
        xdg_positioner_destroy(pos);
        xdg_popup_add_listener(popup, &kPopupListener, this);
        if (parentLayer) zwlr_layer_surface_v1_get_popup(parentLayer, popup);
        if (grabSerial != 0 && gWl.seat) xdg_popup_grab(popup, gWl.seat, grabSerial);
        wl_surface_commit(surface);
        return true;
    }

    ~PopupSurface() override {
        if (popup) xdg_popup_destroy(popup);
        if (xdgSurface) xdg_surface_destroy(xdgSurface);
        popup = nullptr;
        xdgSurface = nullptr;
    }

private:
    static const xdg_surface_listener kXdgSurfaceListener;
    static const xdg_popup_listener kPopupListener;
};

const xdg_surface_listener PopupSurface::kXdgSurfaceListener = {
    .configure = [](void* data, xdg_surface* xs, uint32_t serial) {
        auto* self = static_cast<PopupSurface*>(data);
        xdg_surface_ack_configure(xs, serial);
        self->configured = true;
        self->Redraw();
    },
};

const xdg_popup_listener PopupSurface::kPopupListener = {
    .configure = [](void* data, xdg_popup*, int32_t, int32_t, int32_t w, int32_t h) {
        auto* self = static_cast<PopupSurface*>(data);
        if (w > 0) self->width = w;
        if (h > 0) self->height = h;
    },
    .popup_done = [](void* data, xdg_popup*) {
        auto* self = static_cast<PopupSurface*>(data);
        if (self->onDismissed) {
            auto cb = self->onDismissed;
            RunLater(cb);
        }
    },
    .repositioned = [](void*, xdg_popup*, uint32_t) {},
};

// =========================================================================
// POPUP MENUS (BPopUpMenu / BMenu / BNavMenu replacement)
// =========================================================================
using IconRef = std::shared_ptr<cairo_surface_t>;

static IconRef MakeIconRef(cairo_surface_t* s) {
    if (s == nullptr) return nullptr;
    return IconRef(s, [](cairo_surface_t* p) { cairo_surface_destroy(p); });
}

// Icon cache keyed by name@px -- menus rebuild often (live CPU menu), icons don't change.
static IconRef CachedIcon(const std::string& name, int px, const std::string& extraDir = std::string()) {
    static std::map<std::string, IconRef> cache;
    if (name.empty() || px <= 0) return nullptr;
    std::string key = name + "@" + std::to_string(px) + "@" + extraDir;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IconRef ref = MakeIconRef(gIconTheme->Load(name, px, extraDir));
    cache[key] = ref;
    return ref;
}

enum MenuCheck { kCheckNone = 0, kCheckOff, kCheckOn, kRadioOff, kRadioOn };

struct MenuItem {
    std::string label;
    IconRef icon;                 // drawn at 16x16 logical
    std::string iconName;         // resolved lazily if icon is null
    bool separator = false;
    bool enabled = true;
    bool header = false;          // dim, non-clickable section title
    int check = kCheckNone;
    std::function<void()> action;
    std::function<std::vector<MenuItem>()> submenu;
    unsigned liveMs = 0;          // re-run `submenu` this often while it's open (live CPU/memory menus)
    // BCpuBarMenuItem / BMemoryBarMenuItem style rows.
    double barPercent = -1.0;
    int barPalette = 0;           // 0 = CPU thresholds, 1 = memory thresholds
    std::string valueText;

    static MenuItem Separator() {
        MenuItem m;
        m.separator = true;
        return m;
    }
    static MenuItem Header(const std::string& text) {
        MenuItem m;
        m.label = text;
        m.header = true;
        m.enabled = false;
        return m;
    }
};

class PopupMenu;

class MenuManager {
public:
    // Opens a grabbing menu (click-triggered). Replaces any menu already open.
    void Open(zwlr_layer_surface_v1* parentLayer, const PopupAnchor& anchor, std::vector<MenuItem> items,
        uint32_t serial, std::function<std::vector<MenuItem>()> liveSource = nullptr, unsigned liveIntervalMs = 0);

    // Opens a non-grabbing popup (hover window-title list).
    void OpenHover(zwlr_layer_surface_v1* parentLayer, const PopupAnchor& anchor, std::vector<MenuItem> items,
        bool centered);
    void UpdateHover(std::vector<MenuItem> items);

    void CloseAll();
    void CloseHover();
    bool IsOpen() const { return fRoot != nullptr; }
    bool HoverOpen() const { return fHover != nullptr; }
    bool PointerInHover() const;
    uint64_t LastClosedAt() const { return fClosedAt; }

    std::function<void()> onStateChanged; // lets the dock stop auto-hiding while a menu is up

private:
    friend class PopupMenu;
    std::unique_ptr<PopupMenu> fRoot;
    std::unique_ptr<PopupMenu> fHover;
    uint64_t fClosedAt = 0;
};

static MenuManager gMenus;

class PopupMenu : public PopupSurface {
public:
    PopupMenu(std::vector<MenuItem> items, PopupMenu* parent, bool grabbing, bool centered)
        : fItems(std::move(items)), fParent(parent), fGrabbing(grabbing), fCentered(centered) {
        Measure();
    }

    ~PopupMenu() override {
        if (fLiveTimer) g_source_remove(fLiveTimer);
        if (fSubmenuTimer) g_source_remove(fSubmenuTimer);
        fChild.reset();
    }

    int MeasuredWidth() const { return fWidth; }
    int MeasuredHeight() const { return fHeight; }

    void SetLiveSource(std::function<std::vector<MenuItem>()> src, unsigned intervalMs) {
        fLiveSource = std::move(src);
        if (fLiveSource && intervalMs > 0) {
            fLiveTimer = g_timeout_add(intervalMs, [](gpointer p) -> gboolean {
                auto* self = static_cast<PopupMenu*>(p);
                self->ApplyLiveUpdate(self->fLiveSource());
                return G_SOURCE_CONTINUE;
            }, this);
        }
    }

    // Swaps in fresh items while keeping the popup's size (an xdg_popup can't
    // simply grow) -- rows beyond the original count are dropped, missing rows
    // are left blank, which is how the live CPU/memory menus refresh.
    void ApplyLiveUpdate(std::vector<MenuItem> items) {
        if (fChild) return; // don't reshuffle rows under an open submenu
        fItems = std::move(items);
        if (fItems.size() > fRowCount) fItems.resize(fRowCount);
        if (fHovered >= static_cast<int>(fItems.size())) fHovered = -1;
        Redraw();
    }

    void ReplaceItems(std::vector<MenuItem> items) {
        fItems = std::move(items);
        if (fItems.size() > fRowCount) fItems.resize(fRowCount);
        if (fHovered >= static_cast<int>(fItems.size())) fHovered = -1;
        Redraw();
    }

    void Paint(cairo_t* cr) override {
        const double w = width, h = height;
        RoundedRectPath(cr, 0.5, 0.5, w - 1, h - 1, 6.0);
        cairo_set_source_rgba(cr, 24 / 255.0, 24 / 255.0, 28 / 255.0, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 48 / 255.0, 50 / 255.0, 58 / 255.0, 1.0);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        cairo_save(cr);
        RoundedRectPath(cr, 1, 1, w - 2, h - 2, 5.5);
        cairo_clip(cr);

        double y = kPadY;
        for (size_t i = 0; i < fItems.size(); ++i) {
            const MenuItem& it = fItems[i];
            double rh = RowHeight(it);
            if (it.separator) {
                cairo_set_source_rgba(cr, 60 / 255.0, 62 / 255.0, 72 / 255.0, 1.0);
                cairo_rectangle(cr, 8, std::floor(y + rh / 2), w - 16, 1);
                cairo_fill(cr);
                y += rh;
                continue;
            }
            bool hovered = (static_cast<int>(i) == fHovered) && it.enabled && !it.header;
            bool open = fChild && static_cast<int>(i) == fChildIndex;
            if (hovered || open) {
                cairo_set_source_rgba(cr, 70 / 255.0, 110 / 255.0, 200 / 255.0, 1.0);
                cairo_rectangle(cr, 1, y, w - 2, rh);
                cairo_fill(cr);
            }
            RGBA textColor = it.header ? RGBA{130 / 255.0, 145 / 255.0, 180 / 255.0, 0.9}
                : !it.enabled ? RGBA{0.5, 0.5, 0.55, 1.0}
                : (hovered || open) ? RGBA{1, 1, 1, 1} : RGBA{220 / 255.0, 220 / 255.0, 225 / 255.0, 1};

            double textH = 0;
            MeasureText("Ag", kFontSize, it.header, nullptr, &textH);
            double textY = y + (rh - textH) / 2.0;
            double left = fHasLeftColumn ? kLeftColumn : kPadX;

            if (it.check != kCheckNone) {
                double cx = kPadX + 8, cy = y + rh / 2;
                cairo_new_path(cr); // pango leaves a current point behind
                cairo_set_line_width(cr, 1.2);
                cairo_set_source_rgba(cr, textColor.r, textColor.g, textColor.b, 0.9);
                if (it.check == kRadioOff || it.check == kRadioOn) {
                    cairo_arc(cr, cx, cy, 5, 0, 2 * M_PI);
                    cairo_stroke(cr);
                    if (it.check == kRadioOn) {
                        cairo_arc(cr, cx, cy, 2.5, 0, 2 * M_PI);
                        cairo_fill(cr);
                    }
                } else {
                    cairo_rectangle(cr, cx - 5, cy - 5, 10, 10);
                    cairo_stroke(cr);
                    if (it.check == kCheckOn) {
                        cairo_move_to(cr, cx - 3, cy);
                        cairo_line_to(cr, cx - 1, cy + 3);
                        cairo_line_to(cr, cx + 4, cy - 3);
                        cairo_stroke(cr);
                    }
                }
            } else {
                IconRef icon = it.icon;
                if (!icon && !it.iconName.empty()) icon = CachedIcon(it.iconName, static_cast<int>(std::ceil(16 * scale)));
                if (icon) {
                    double s = 16.0 / cairo_image_surface_get_width(icon.get());
                    cairo_save(cr);
                    cairo_translate(cr, kPadX, y + (rh - 16) / 2);
                    cairo_scale(cr, s, s);
                    cairo_set_source_surface(cr, icon.get(), 0, 0);
                    if (!it.enabled) cairo_paint_with_alpha(cr, 0.45);
                    else cairo_paint(cr);
                    cairo_restore(cr);
                }
            }

            if (it.barPercent >= 0.0) {
                // Name / percentage / capsule bar columns, as BCpuBarMenuItem draws them.
                DrawText(cr, it.label, left, textY, kFontSize, false, textColor, fNameColumnW - 8);
                double valueX = left + fNameColumnW;
                DrawText(cr, it.valueText, valueX, textY, kFontSize, false, textColor);
                double barX = valueX + kValueColumnW;
                double barW = kBarW, barH = 10, barY = y + (rh - barH) / 2;
                double pct = std::clamp(it.barPercent, 0.0, 100.0);
                cairo_set_source_rgb(cr, 45 / 255.0, 45 / 255.0, 45 / 255.0);
                cairo_rectangle(cr, barX, barY, barW, barH);
                cairo_fill(cr);
                RGBA fill;
                if (it.barPalette == 1) {
                    if (it.barPercent > 85.0) fill = {50 / 255.0, 205 / 255.0, 50 / 255.0, 1};
                    else if (it.barPercent > 60.0) fill = {220 / 255.0, 20 / 255.0, 60 / 255.0, 1};
                    else fill = {1.0, 140 / 255.0, 0, 1};
                } else {
                    if (it.barPercent > 75.0) fill = {50 / 255.0, 205 / 255.0, 50 / 255.0, 1};
                    else if (it.barPercent > 35.0) fill = {1.0, 140 / 255.0, 0, 1};
                    else fill = {50 / 255.0, 205 / 255.0, 50 / 255.0, 1};
                }
                if (pct > 0) {
                    cairo_set_source_rgba(cr, fill.r, fill.g, fill.b, 1);
                    cairo_rectangle(cr, barX, barY, barW * pct / 100.0, barH);
                    cairo_fill(cr);
                }
                cairo_set_source_rgb(cr, 90 / 255.0, 90 / 255.0, 90 / 255.0);
                cairo_set_line_width(cr, 1);
                cairo_rectangle(cr, barX + 0.5, barY + 0.5, barW - 1, barH - 1);
                cairo_stroke(cr);
            } else if (fCentered) {
                DrawText(cr, it.label, 4, textY, kFontSize, it.header, textColor, w - 8, true);
            } else {
                double maxW = w - left - kPadX - (it.submenu ? 14 : 0);
                DrawText(cr, it.label, left, textY, kFontSize, it.header, textColor, maxW);
            }

            if (it.submenu) {
                double ax = w - kPadX - 4, ay = y + rh / 2;
                cairo_new_path(cr);
                cairo_set_source_rgba(cr, textColor.r, textColor.g, textColor.b, 0.9);
                cairo_move_to(cr, ax - 4, ay - 4);
                cairo_line_to(cr, ax, ay);
                cairo_line_to(cr, ax - 4, ay + 4);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
            }
            y += rh;
        }
        cairo_restore(cr);
    }

    void PointerMotion(double x, double y) override {
        fPointerInside = true;
        int row = RowAt(y);
        if (row != fHovered) {
            fHovered = row;
            Redraw();
            ScheduleSubmenu();
        }
    }
    void PointerEnter(double x, double y) override { PointerMotion(x, y); }
    void PointerLeave() override {
        fPointerInside = false;
        if (!fChild && fHovered != -1) {
            fHovered = -1;
            Redraw();
        }
        if (onPointerLeave) onPointerLeave();
    }

    void PointerButton(int button, bool pressed, uint32_t serial) override {
        if (pressed) return; // act on release, like BMenu does after Go()
        int row = RowAt(gWl.pointerY);
        if (row < 0) return;
        Activate(row);
    }

    void Key(xkb_keysym_t sym, const std::string&) override {
        if (sym == XKB_KEY_Escape) {
            if (fParent) fParent->CloseChild();
            else gMenus.CloseAll();
        } else if (sym == XKB_KEY_Down || sym == XKB_KEY_Up) {
            int n = static_cast<int>(fItems.size());
            for (int step = 1; step <= n; ++step) {
                int idx = ((fHovered < 0 ? (sym == XKB_KEY_Down ? -1 : 0) : fHovered)
                    + (sym == XKB_KEY_Down ? step : -step) + n * 2) % n;
                if (Selectable(idx)) { fHovered = idx; break; }
            }
            Redraw();
        } else if ((sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter || sym == XKB_KEY_Right) && fHovered >= 0) {
            Activate(fHovered);
        } else if (sym == XKB_KEY_Left && fParent) {
            fParent->CloseChild();
        }
    }

    std::function<void()> onPointerLeave;
    bool PointerInside() const {
        if (fPointerInside) return true;
        return fChild && fChild->PointerInside();
    }

    void CloseChild() {
        if (fSubmenuTimer) { g_source_remove(fSubmenuTimer); fSubmenuTimer = 0; }
        if (fChild) {
            // Deferred: we may be inside one of the child's own callbacks.
            PopupMenu* raw = fChild.release();
            fChildIndex = -1;
            RunLater([raw]() { delete raw; });
            Redraw();
        }
    }

private:
    static constexpr double kFontSize = 12.0;
    static constexpr double kRowH = 22.0;
    static constexpr double kSepH = 9.0;
    static constexpr double kPadX = 8.0;
    static constexpr double kPadY = 4.0;
    static constexpr double kLeftColumn = 30.0;
    static constexpr double kValueColumnW = 54.0;
    static constexpr double kBarW = 80.0;

    static double RowHeight(const MenuItem& it) { return it.separator ? kSepH : kRowH; }

    bool Selectable(int i) const {
        return i >= 0 && i < static_cast<int>(fItems.size()) && !fItems[i].separator && !fItems[i].header && fItems[i].enabled;
    }

    void Measure() {
        fHasLeftColumn = false;
        bool hasBars = false;
        double maxLabel = 0;
        double h = kPadY * 2;
        for (const auto& it : fItems) {
            h += RowHeight(it);
            if (it.separator) continue;
            if (it.icon || !it.iconName.empty() || it.check != kCheckNone) fHasLeftColumn = true;
            if (it.barPercent >= 0) hasBars = true;
            double tw = 0;
            MeasureText(it.label, kFontSize, it.header, &tw, nullptr);
            maxLabel = std::max(maxLabel, tw);
        }
        double left = fHasLeftColumn ? kLeftColumn : kPadX;
        double w;
        if (hasBars) {
            fNameColumnW = std::clamp(maxLabel + 16, 120.0, 280.0);
            w = left + fNameColumnW + kValueColumnW + kBarW + kPadX + 4;
        } else if (fCentered) {
            w = std::clamp(maxLabel + 16, 100.0, 320.0);
        } else {
            w = std::clamp(left + maxLabel + kPadX + 22, 150.0, 420.0);
        }
        fWidth = static_cast<int>(std::ceil(w));
        fHeight = static_cast<int>(std::ceil(h));
        fRowCount = fItems.size();
    }

    int RowAt(double y) const {
        double top = kPadY;
        for (size_t i = 0; i < fItems.size(); ++i) {
            double rh = RowHeight(fItems[i]);
            if (y >= top && y < top + rh) return fItems[i].separator ? -1 : static_cast<int>(i);
            top += rh;
        }
        return -1;
    }

    double RowTop(int row) const {
        double top = kPadY;
        for (int i = 0; i < row && i < static_cast<int>(fItems.size()); ++i) top += RowHeight(fItems[i]);
        return top;
    }

    void ScheduleSubmenu() {
        if (fSubmenuTimer) { g_source_remove(fSubmenuTimer); fSubmenuTimer = 0; }
        if (fChild && fChildIndex == fHovered) return;
        fSubmenuTimer = g_timeout_add(180, [](gpointer p) -> gboolean {
            auto* self = static_cast<PopupMenu*>(p);
            self->fSubmenuTimer = 0;
            if (self->fChild && self->fChildIndex != self->fHovered) self->CloseChild();
            if (self->Selectable(self->fHovered) && self->fItems[self->fHovered].submenu && !self->fChild) {
                self->OpenSubmenu(self->fHovered);
            }
            return G_SOURCE_REMOVE;
        }, this);
    }

    void OpenSubmenu(int row) {
        std::vector<MenuItem> sub = fItems[row].submenu();
        if (sub.empty()) {
            MenuItem empty;
            empty.label = "(empty)";
            empty.enabled = false;
            sub.push_back(empty);
        }
        auto child = std::make_unique<PopupMenu>(std::move(sub), this, fGrabbing, false);
        PopupAnchor a;
        a.x = 0;
        a.y = static_cast<int>(RowTop(row));
        a.w = width;
        a.h = static_cast<int>(kRowH);
        a.anchor = XDG_POSITIONER_ANCHOR_TOP_RIGHT;
        a.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
        a.constraints = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
        a.offsetY = -static_cast<int>(kPadY);
        PopupMenu* raw = child.get();
        uint32_t serial = fGrabbing ? gWl.lastInputSerial : 0;
        if (!raw->CreatePopup(nullptr, xdgSurface, a, raw->MeasuredWidth(), raw->MeasuredHeight(), serial)) return;
        raw->onDismissed = [this]() { CloseChild(); };
        if (fItems[row].liveMs > 0) raw->SetLiveSource(fItems[row].submenu, fItems[row].liveMs);
        fChild = std::move(child);
        fChildIndex = row;
        Redraw();
    }

    void Activate(int row) {
        if (!Selectable(row)) return;
        MenuItem& it = fItems[row];
        if (it.submenu) {
            if (!fChild || fChildIndex != row) {
                CloseChild();
                OpenSubmenu(row);
            }
            return;
        }
        if (!it.action) return;
        auto action = it.action;
        // Close first, then run -- an action often opens another surface.
        if (fGrabbing) gMenus.CloseAll();
        RunLater(action);
    }

    std::vector<MenuItem> fItems;
    size_t fRowCount = 0;
    PopupMenu* fParent = nullptr;
    std::unique_ptr<PopupMenu> fChild;
    int fChildIndex = -1;
    bool fGrabbing = true;
    bool fCentered = false;
    bool fHasLeftColumn = false;
    bool fPointerInside = false;
    double fNameColumnW = 200;
    int fWidth = 150, fHeight = 30;
    int fHovered = -1;
    guint fSubmenuTimer = 0;
    guint fLiveTimer = 0;
    std::function<std::vector<MenuItem>()> fLiveSource;
};

void MenuManager::Open(zwlr_layer_surface_v1* parentLayer, const PopupAnchor& anchor, std::vector<MenuItem> items,
    uint32_t serial, std::function<std::vector<MenuItem>()> liveSource, unsigned liveIntervalMs) {
    CloseAll();
    CloseHover();
    if (items.empty()) return;
    auto menu = std::make_unique<PopupMenu>(std::move(items), nullptr, true, false);
    if (!menu->CreatePopup(parentLayer, nullptr, anchor, menu->MeasuredWidth(), menu->MeasuredHeight(), serial)) return;
    menu->onDismissed = [this]() { CloseAll(); };
    if (liveSource) menu->SetLiveSource(std::move(liveSource), liveIntervalMs);
    fRoot = std::move(menu);
    if (onStateChanged) onStateChanged();
}

void MenuManager::OpenHover(zwlr_layer_surface_v1* parentLayer, const PopupAnchor& anchor, std::vector<MenuItem> items,
    bool centered) {
    CloseHover();
    if (items.empty() || fRoot) return;
    auto menu = std::make_unique<PopupMenu>(std::move(items), nullptr, false, centered);
    if (!menu->CreatePopup(parentLayer, nullptr, anchor, menu->MeasuredWidth(), menu->MeasuredHeight(), 0)) return;
    menu->onDismissed = [this]() { CloseHover(); };
    fHover = std::move(menu);
    if (onStateChanged) onStateChanged();
}

void MenuManager::UpdateHover(std::vector<MenuItem> items) {
    if (fHover) fHover->ReplaceItems(std::move(items));
}

bool MenuManager::PointerInHover() const {
    return fHover && fHover->PointerInside();
}

void MenuManager::CloseAll() {
    if (fRoot) {
        PopupMenu* raw = fRoot.release();
        RunLater([raw]() { delete raw; });
        fClosedAt = NowMs();
        if (onStateChanged) onStateChanged();
    }
}

void MenuManager::CloseHover() {
    if (fHover) {
        PopupMenu* raw = fHover.release();
        RunLater([raw]() { delete raw; });
        if (onStateChanged) onStateChanged();
    }
}

// Tracker's BNavMenu, rebuilt on plain directories: folders open as lazily
// populated submenus, files and folders both open in the default handler.
static std::vector<MenuItem> BuildNavMenu(const std::string& dir) {
    std::vector<MenuItem> items;
    {
        MenuItem open;
        open.label = "Open this folder";
        open.iconName = "folder-open";
        open.action = [dir]() { OpenPath(dir); };
        items.push_back(open);
        items.push_back(MenuItem::Separator());
    }
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return items;
    struct Entry { std::string name; bool isDir; };
    std::vector<Entry> entries;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue; // hidden files, like Tracker's default
        std::string full = dir + "/" + e->d_name;
        entries.push_back({e->d_name, IsDirectory(full)});
    }
    closedir(d);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
    });
    const size_t kMaxEntries = 60;
    for (size_t i = 0; i < entries.size() && i < kMaxEntries; ++i) {
        const Entry& e = entries[i];
        std::string full = (dir == "/" ? "" : dir) + "/" + e.name;
        MenuItem item;
        item.label = e.name;
        if (e.isDir) {
            item.iconName = "folder";
            item.submenu = [full]() { return BuildNavMenu(full); };
        } else {
            char* type = g_content_type_guess(full.c_str(), nullptr, 0, nullptr);
            GIcon* gicon = type ? g_content_type_get_icon(type) : nullptr;
            item.iconName = AppDatabase::IconNameFor(gicon);
            if (gicon) g_object_unref(gicon);
            g_free(type);
            item.action = [full]() { OpenPath(full); };
        }
        items.push_back(item);
    }
    if (entries.size() > kMaxEntries) {
        MenuItem more;
        more.label = "More...";
        more.action = [dir]() { OpenPath(dir); };
        items.push_back(more);
    }
    return items;
}

static std::string XdgUserDir(GUserDirectory which, const char* fallback) {
    const char* d = g_get_user_special_dir(which);
    if (d && *d) return d;
    return HomeDir() + "/" + fallback;
}

static std::vector<MenuItem> BuildPlacesMenu() {
    std::vector<MenuItem> items;
    auto addPlace = [&](const std::string& label, const std::string& icon, const std::string& path) {
        if (!IsDirectory(path)) return;
        MenuItem m;
        m.label = label;
        m.iconName = icon;
        m.submenu = [path]() { return BuildNavMenu(path); };
        items.push_back(m);
    };
    addPlace("Home", "user-home", HomeDir());
    addPlace("Desktop", "user-desktop", XdgUserDir(G_USER_DIRECTORY_DESKTOP, "Desktop"));
    addPlace("Documents", "folder-documents", XdgUserDir(G_USER_DIRECTORY_DOCUMENTS, "Documents"));
    addPlace("Downloads", "folder-download", XdgUserDir(G_USER_DIRECTORY_DOWNLOAD, "Downloads"));
    addPlace("Music", "folder-music", XdgUserDir(G_USER_DIRECTORY_MUSIC, "Music"));
    addPlace("Pictures", "folder-pictures", XdgUserDir(G_USER_DIRECTORY_PICTURES, "Pictures"));
    addPlace("Videos", "folder-videos", XdgUserDir(G_USER_DIRECTORY_VIDEOS, "Videos"));
    items.push_back(MenuItem::Separator());
    addPlace("File System", "drive-harddisk", "/");
    return items;
}

// =========================================================================
// EGL / OPENGL HELPERS FOR THE DOCK
// =========================================================================
// The dock keeps the Haiku build's fixed-function GL 2.1 rendering. Every
// texture and color is kept *premultiplied* (cairo's native format, and what
// Wayland compositors expect in the buffer), so one blend mode covers
// everything: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
struct GLTexture {
    GLuint id = 0;
    int width = 0, height = 0;         // device pixels
    double logicalW = 0, logicalH = 0; // logical size it should be drawn at
};

static void DeleteTexture(GLTexture& t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t = GLTexture();
}

static GLTexture UploadTexture(cairo_surface_t* s, double logicalW = -1, double logicalH = -1) {
    GLTexture t;
    if (s == nullptr) return t;
    cairo_surface_flush(s);
    t.width = cairo_image_surface_get_width(s);
    t.height = cairo_image_surface_get_height(s);
    int stride = cairo_image_surface_get_stride(s);
    t.logicalW = logicalW > 0 ? logicalW : t.width;
    t.logicalH = logicalH > 0 ? logicalH : t.height;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    // cairo ARGB32 is native-endian 0xAARRGGBB == BGRA bytes on little-endian.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_BGRA,
        GL_UNSIGNED_INT_8_8_8_8_REV, cairo_image_surface_get_data(s));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

static GLTexture UploadImage(Image& img) {
    return UploadTexture(img.surface, img.logicalW, img.logicalH);
}

struct HRect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool Contains(float x, float y) const { return x >= left && x <= right && y >= top && y <= bottom; }
    float Width() const { return right - left; }
    float Height() const { return bottom - top; }
};

// Straight (non-premultiplied) color in, premultiplied out.
static void SetColor(float r, float g, float b, float a) {
    glColor4f(r * a, g * a, b * a, a);
}

static void DrawFilledRect(const HRect& rc, float r, float g, float b, float a) {
    SetColor(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(rc.left, rc.top);
    glVertex2f(rc.right, rc.top);
    glVertex2f(rc.right, rc.bottom);
    glVertex2f(rc.left, rc.bottom);
    glEnd();
}

static void DrawRectOutline(const HRect& rc, float r, float g, float b, float a) {
    SetColor(r, g, b, a);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(rc.left + 0.5f, rc.top + 0.5f);
    glVertex2f(rc.right - 0.5f, rc.top + 0.5f);
    glVertex2f(rc.right - 0.5f, rc.bottom - 0.5f);
    glVertex2f(rc.left + 0.5f, rc.bottom - 0.5f);
    glEnd();
}

static void CornerArc(float cx, float cy, float radius, float startDeg, float endDeg) {
    const float degToRad = 3.14159265f / 180.0f;
    for (float a = startDeg; a <= endDeg + 0.01f; a += 10.0f) {
        glVertex2f(cx + radius * std::cos(a * degToRad), cy + radius * std::sin(a * degToRad));
    }
}

static void DrawFilledRoundedRect(const HRect& rc, float radius, float r, float g, float b, float a) {
    radius = std::min(radius, std::min(rc.Width(), rc.Height()) / 2.0f);
    SetColor(r, g, b, a);
    glBegin(GL_POLYGON);
    CornerArc(rc.right - radius, rc.top + radius, radius, 270.0f, 360.0f);
    CornerArc(rc.right - radius, rc.bottom - radius, radius, 0.0f, 90.0f);
    CornerArc(rc.left + radius, rc.bottom - radius, radius, 90.0f, 180.0f);
    CornerArc(rc.left + radius, rc.top + radius, radius, 180.0f, 270.0f);
    glEnd();
}

static void DrawOutlineRoundedRect(const HRect& rc, float radius, float r, float g, float b, float a) {
    radius = std::min(radius, std::min(rc.Width(), rc.Height()) / 2.0f);
    SetColor(r, g, b, a);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    CornerArc(rc.right - radius, rc.top + radius, radius, 270.0f, 360.0f);
    CornerArc(rc.right - radius, rc.bottom - radius, radius, 0.0f, 90.0f);
    CornerArc(rc.left + radius, rc.bottom - radius, radius, 90.0f, 180.0f);
    CornerArc(rc.left + radius, rc.top + radius, radius, 180.0f, 270.0f);
    glEnd();
}

// Draws a texture into a logical rect at the given opacity.
static void DrawTexture(const GLTexture& t, float left, float top, float w, float h, float alpha = 1.0f) {
    if (t.id == 0) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glColor4f(alpha, alpha, alpha, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(left, top);
    glTexCoord2f(1, 0); glVertex2f(left + w, top);
    glTexCoord2f(1, 1); glVertex2f(left + w, top + h);
    glTexCoord2f(0, 1); glVertex2f(left, top + h);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
};
static EglState gEgl;

static bool InitEgl() {
    gEgl.display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gWl.display));
    if (gEgl.display == EGL_NO_DISPLAY) return false;
    EGLint major = 0, minor = 0;
    if (!eglInitialize(gEgl.display, &major, &minor)) return false;
    if (!eglBindAPI(EGL_OPENGL_API)) {
        WarnLog("EGL has no desktop OpenGL support\n");
        return false;
    }
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLint count = 0;
    if (!eglChooseConfig(gEgl.display, configAttribs, &gEgl.config, 1, &count) || count == 0) return false;
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 2,
        EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_NONE
    };
    gEgl.context = eglCreateContext(gEgl.display, gEgl.config, EGL_NO_CONTEXT, contextAttribs);
    if (gEgl.context == EGL_NO_CONTEXT) return false;
    DebugLog("EGL %d.%d ready\n", major, minor);
    return true;
}

// =========================================================================
// TOPLEVEL (TASKBAR) BACKENDS
// =========================================================================
// One Toplevel per window, from whichever protocol the compositor offers:
//   * zwlr_foreign_toplevel_manager_v1 -- Hyprland, Sway, niri, labwc, Wayfire...
//   * org_kde_plasma_window_management -- KWin
// The dock groups them by app_id into one icon per application, the way the
// Haiku build groups windows by team.
struct Toplevel {
    uint64_t uid = 0;
    std::string title;
    std::string appId;
    std::string themedIcon;     // KWin tells us the icon name directly
    bool activated = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    bool skipTaskbar = false;
    bool hasParent = false;     // transient dialogs are folded into their parent
    bool ready = false;
    bool onAllDesktops = false;
    uint32_t pid = 0;
    std::set<std::string> desktops;  // KWin virtual desktop ids
    uint64_t lastActivated = 0;
    zwlr_foreign_toplevel_handle_v1* wlr = nullptr;
    org_kde_plasma_window* plasma = nullptr;
};

class ToplevelManager {
public:
    std::function<void()> onChanged;

    bool Available() const { return fBackend != kNone; }
    const char* BackendName() const {
        return fBackend == kPlasma ? "KDE plasma-window-management"
            : fBackend == kWlr ? "wlr-foreign-toplevel-management" : "none";
    }

    void Init() {
        if (gWl.plasmaWindows) {
            fBackend = kPlasma;
            org_kde_plasma_window_management_add_listener(gWl.plasmaWindows, &kPlasmaMgmtListener, this);
        } else if (gWl.wlrToplevels) {
            fBackend = kWlr;
            zwlr_foreign_toplevel_manager_v1_add_listener(gWl.wlrToplevels, &kWlrMgrListener, this);
        }
        DebugLog("taskbar backend: %s\n", BackendName());
    }

    // Windows eligible for the dock, in the order they first appeared.
    std::vector<Toplevel*> Windows() const {
        std::vector<Toplevel*> out;
        for (const auto& t : fWindows) {
            if (t->ready && !t->skipTaskbar && !t->hasParent) out.push_back(t.get());
        }
        return out;
    }

    void Activate(Toplevel* t) {
        if (t == nullptr) return;
        DebugLog("activate window '%s' (%s)\n", t->title.c_str(), t->appId.c_str());
        if (t->wlr && gWl.seat) {
            if (t->minimized) zwlr_foreign_toplevel_handle_v1_unset_minimized(t->wlr);
            zwlr_foreign_toplevel_handle_v1_activate(t->wlr, gWl.seat);
        } else if (t->plasma) {
            org_kde_plasma_window_set_state(t->plasma,
                ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE | ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED,
                ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE);
        }
    }

    void Unminimize(Toplevel* t) {
        if (t == nullptr) return;
        if (t->wlr) zwlr_foreign_toplevel_handle_v1_unset_minimized(t->wlr);
        else if (t->plasma) org_kde_plasma_window_set_state(t->plasma, ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED, 0);
    }

    void Minimize(Toplevel* t) {
        if (t == nullptr) return;
        if (t->wlr) zwlr_foreign_toplevel_handle_v1_set_minimized(t->wlr);
        else if (t->plasma) {
            org_kde_plasma_window_set_state(t->plasma, ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED,
                ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED);
        }
    }

    void Close(Toplevel* t) {
        if (t == nullptr) return;
        if (t->wlr) zwlr_foreign_toplevel_handle_v1_close(t->wlr);
        else if (t->plasma) org_kde_plasma_window_close(t->plasma);
    }

    // Tells the compositor where the window's dock icon is, so minimize
    // animations fly into it.
    void SetIconRect(Toplevel* t, wl_surface* dockSurface, int x, int y, int w, int h) {
        if (t == nullptr || dockSurface == nullptr || w <= 0 || h <= 0) return;
        if (t->wlr) zwlr_foreign_toplevel_handle_v1_set_rectangle(t->wlr, dockSurface, x, y, w, h);
        else if (t->plasma) {
            org_kde_plasma_window_set_minimized_geometry(t->plasma, dockSurface,
                std::max(0, x), std::max(0, y), w, h);
        }
    }

    bool IsOnDesktop(const Toplevel* t, const std::string& desktopId) const {
        if (fBackend != kPlasma || desktopId.empty()) return true;
        return t->onAllDesktops || t->desktops.empty() || t->desktops.count(desktopId) > 0;
    }

private:
    enum Backend { kNone, kWlr, kPlasma };

    Toplevel* NewWindow() {
        auto t = std::make_unique<Toplevel>();
        t->uid = ++fNextUid;
        Toplevel* raw = t.get();
        fWindows.push_back(std::move(t));
        return raw;
    }

    void Remove(Toplevel* t) {
        for (auto it = fWindows.begin(); it != fWindows.end(); ++it) {
            if (it->get() == t) {
                if (t->wlr) zwlr_foreign_toplevel_handle_v1_destroy(t->wlr);
                if (t->plasma) org_kde_plasma_window_destroy(t->plasma);
                fWindows.erase(it);
                break;
            }
        }
        Changed();
    }

    void Changed() {
        if (fChangePending) return;
        fChangePending = true;
        RunLater([this]() {
            fChangePending = false;
            if (onChanged) onChanged();
        });
    }

    Toplevel* FindWlr(zwlr_foreign_toplevel_handle_v1* h) {
        for (const auto& t : fWindows) if (t->wlr == h) return t.get();
        return nullptr;
    }

    static void SetActivated(Toplevel* t, bool active) {
        if (active && !t->activated) t->lastActivated = NowMs();
        t->activated = active;
    }

    static const zwlr_foreign_toplevel_manager_v1_listener kWlrMgrListener;
    static const zwlr_foreign_toplevel_handle_v1_listener kWlrHandleListener;
    static const org_kde_plasma_window_management_listener kPlasmaMgmtListener;
    static const org_kde_plasma_window_listener kPlasmaWindowListener;

    Backend fBackend = kNone;
    std::vector<std::unique_ptr<Toplevel>> fWindows;
    uint64_t fNextUid = 0;
    bool fChangePending = false;
};

static ToplevelManager gToplevels;

struct WlrHandleCtx { ToplevelManager* mgr; Toplevel* t; };

const zwlr_foreign_toplevel_manager_v1_listener ToplevelManager::kWlrMgrListener = {
    .toplevel = [](void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* h) {
        auto* self = static_cast<ToplevelManager*>(data);
        Toplevel* t = self->NewWindow();
        t->wlr = h;
        zwlr_foreign_toplevel_handle_v1_add_listener(h, &kWlrHandleListener, self);
    },
    .finished = [](void*, zwlr_foreign_toplevel_manager_v1*) {},
};

const zwlr_foreign_toplevel_handle_v1_listener ToplevelManager::kWlrHandleListener = {
    .title = [](void* data, zwlr_foreign_toplevel_handle_v1* h, const char* title) {
        if (Toplevel* t = static_cast<ToplevelManager*>(data)->FindWlr(h)) t->title = title ? title : "";
    },
    .app_id = [](void* data, zwlr_foreign_toplevel_handle_v1* h, const char* appId) {
        if (Toplevel* t = static_cast<ToplevelManager*>(data)->FindWlr(h)) t->appId = appId ? appId : "";
    },
    .output_enter = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
    .output_leave = [](void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {},
    .state = [](void* data, zwlr_foreign_toplevel_handle_v1* h, wl_array* state) {
        Toplevel* t = static_cast<ToplevelManager*>(data)->FindWlr(h);
        if (t == nullptr) return;
        bool active = false;
        t->minimized = t->maximized = t->fullscreen = false;
        const uint32_t* states = static_cast<const uint32_t*>(state->data);
        for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i) {
            const uint32_t* s = &states[i];
            if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) active = true;
            else if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) t->minimized = true;
            else if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) t->maximized = true;
            else if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) t->fullscreen = true;
        }
        SetActivated(t, active);
    },
    .done = [](void* data, zwlr_foreign_toplevel_handle_v1* h) {
        auto* self = static_cast<ToplevelManager*>(data);
        if (Toplevel* t = self->FindWlr(h)) {
            t->ready = true;
            self->Changed();
        }
    },
    .closed = [](void* data, zwlr_foreign_toplevel_handle_v1* h) {
        auto* self = static_cast<ToplevelManager*>(data);
        if (Toplevel* t = self->FindWlr(h)) self->Remove(t);
    },
    .parent = [](void* data, zwlr_foreign_toplevel_handle_v1* h, zwlr_foreign_toplevel_handle_v1* parent) {
        if (Toplevel* t = static_cast<ToplevelManager*>(data)->FindWlr(h)) t->hasParent = (parent != nullptr);
    },
};

const org_kde_plasma_window_management_listener ToplevelManager::kPlasmaMgmtListener = {
    .show_desktop_changed = [](void*, org_kde_plasma_window_management*, uint32_t) {},
    .window = [](void*, org_kde_plasma_window_management*, uint32_t) {},  // deprecated; window_with_uuid follows
    .stacking_order_changed = [](void*, org_kde_plasma_window_management*, wl_array*) {},
    .stacking_order_uuid_changed = [](void*, org_kde_plasma_window_management*, const char*) {},
    .window_with_uuid = [](void* data, org_kde_plasma_window_management* mgmt, uint32_t, const char* uuid) {
        auto* self = static_cast<ToplevelManager*>(data);
        Toplevel* t = self->NewWindow();
        t->plasma = org_kde_plasma_window_management_get_window_by_uuid(mgmt, uuid);
        org_kde_plasma_window_add_listener(t->plasma, &kPlasmaWindowListener, new WlrHandleCtx{self, t});
    },
    .stacking_order_changed_2 = [](void*, org_kde_plasma_window_management*) {},
};

#define PLASMA_CTX(data) auto* ctx = static_cast<WlrHandleCtx*>(data); Toplevel* t = ctx->t; (void)t

const org_kde_plasma_window_listener ToplevelManager::kPlasmaWindowListener = {
    .title_changed = [](void* data, org_kde_plasma_window*, const char* title) {
        PLASMA_CTX(data);
        t->title = title ? title : "";
        if (t->ready) ctx->mgr->Changed();
    },
    .app_id_changed = [](void* data, org_kde_plasma_window*, const char* appId) {
        PLASMA_CTX(data);
        t->appId = appId ? appId : "";
        if (t->ready) ctx->mgr->Changed();
    },
    .state_changed = [](void* data, org_kde_plasma_window*, uint32_t flags) {
        PLASMA_CTX(data);
        SetActivated(t, flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE);
        t->minimized = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
        t->maximized = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED;
        t->fullscreen = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_FULLSCREEN;
        t->skipTaskbar = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
        t->onAllDesktops = flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ON_ALL_DESKTOPS;
        if (t->ready) ctx->mgr->Changed();
    },
    .virtual_desktop_changed = [](void*, org_kde_plasma_window*, int32_t) {},
    .themed_icon_name_changed = [](void* data, org_kde_plasma_window*, const char* name) {
        PLASMA_CTX(data);
        t->themedIcon = name ? name : "";
        if (t->ready) ctx->mgr->Changed();
    },
    .unmapped = [](void* data, org_kde_plasma_window*) {
        auto* ctx = static_cast<WlrHandleCtx*>(data);
        ToplevelManager* mgr = ctx->mgr;
        Toplevel* t = ctx->t;
        delete ctx;
        mgr->Remove(t);
    },
    .initial_state = [](void* data, org_kde_plasma_window*) {
        PLASMA_CTX(data);
        t->ready = true;
        ctx->mgr->Changed();
    },
    .parent_window = [](void* data, org_kde_plasma_window*, org_kde_plasma_window* parent) {
        PLASMA_CTX(data);
        t->hasParent = (parent != nullptr);
    },
    .geometry = [](void*, org_kde_plasma_window*, int32_t, int32_t, uint32_t, uint32_t) {},
    .icon_changed = [](void*, org_kde_plasma_window*) {},
    .pid_changed = [](void* data, org_kde_plasma_window*, uint32_t pid) {
        PLASMA_CTX(data);
        t->pid = pid;
    },
    .virtual_desktop_entered = [](void* data, org_kde_plasma_window*, const char* id) {
        PLASMA_CTX(data);
        if (id) t->desktops.insert(id);
        if (t->ready) ctx->mgr->Changed();
    },
    .virtual_desktop_left = [](void* data, org_kde_plasma_window*, const char* id) {
        PLASMA_CTX(data);
        if (id) t->desktops.erase(id);
        if (t->ready) ctx->mgr->Changed();
    },
    .application_menu = [](void*, org_kde_plasma_window*, const char*, const char*) {},
    .activity_entered = [](void*, org_kde_plasma_window*, const char*) {},
    .activity_left = [](void*, org_kde_plasma_window*, const char*) {},
    .resource_name_changed = [](void*, org_kde_plasma_window*, const char*) {},
    .client_geometry = [](void*, org_kde_plasma_window*, int32_t, int32_t, uint32_t, uint32_t) {},
    .mapped = [](void*, org_kde_plasma_window*) {},
};

// =========================================================================
// MINIMAL JSON READER (Hyprland / Sway IPC replies)
// =========================================================================
struct Json {
    enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json* Get(const char* key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double Num(const char* key, double def = 0) const {
        const Json* j = Get(key);
        return (j && j->type == kNumber) ? j->num : def;
    }
    std::string Str(const char* key) const {
        const Json* j = Get(key);
        return (j && j->type == kString) ? j->str : std::string();
    }
    bool Bool(const char* key) const {
        const Json* j = Get(key);
        return j && j->type == kBool && j->b;
    }

    static Json Parse(const std::string& text) {
        size_t pos = 0;
        return ParseValue(text, pos);
    }

private:
    static void Skip(const std::string& s, size_t& p) {
        while (p < s.size() && isspace(static_cast<unsigned char>(s[p]))) ++p;
    }
    static Json ParseValue(const std::string& s, size_t& p) {
        Skip(s, p);
        Json j;
        if (p >= s.size()) return j;
        char c = s[p];
        if (c == '{') {
            j.type = kObject;
            ++p;
            while (true) {
                Skip(s, p);
                if (p >= s.size()) break;
                if (s[p] == '}') { ++p; break; }
                if (s[p] == ',') { ++p; continue; }
                Json key = ParseValue(s, p);
                Skip(s, p);
                if (p < s.size() && s[p] == ':') ++p;
                Json value = ParseValue(s, p);
                j.obj.emplace_back(key.str, std::move(value));
            }
        } else if (c == '[') {
            j.type = kArray;
            ++p;
            while (true) {
                Skip(s, p);
                if (p >= s.size()) break;
                if (s[p] == ']') { ++p; break; }
                if (s[p] == ',') { ++p; continue; }
                j.arr.push_back(ParseValue(s, p));
            }
        } else if (c == '"') {
            j.type = kString;
            ++p;
            while (p < s.size() && s[p] != '"') {
                if (s[p] == '\\' && p + 1 < s.size()) {
                    char e = s[p + 1];
                    p += 2;
                    switch (e) {
                        case 'n': j.str += '\n'; break;
                        case 't': j.str += '\t'; break;
                        case 'u':
                            if (p + 4 <= s.size()) {
                                gunichar ch = static_cast<gunichar>(strtoul(s.substr(p, 4).c_str(), nullptr, 16));
                                char buf[8];
                                int n = g_unichar_to_utf8(ch, buf);
                                j.str.append(buf, n);
                                p += 4;
                            }
                            break;
                        default: j.str += e; break;
                    }
                } else {
                    j.str += s[p++];
                }
            }
            ++p;
        } else if (c == 't' || c == 'f') {
            j.type = kBool;
            j.b = (c == 't');
            p += j.b ? 4 : 5;
        } else if (c == 'n') {
            p += 4;
        } else {
            j.type = kNumber;
            char* end = nullptr;
            j.num = strtod(s.c_str() + p, &end);
            p = end ? static_cast<size_t>(end - s.c_str()) : p + 1;
        }
        return j;
    }
};

// =========================================================================
// WORKSPACE SWITCHER BACKENDS
// =========================================================================
struct WorkspaceInfo {
    std::string id;
    std::string name;
    bool active = false;
    int sortKey = 0;
    void* handle = nullptr;
};

class WorkspaceManager {
public:
    std::function<void()> onChanged;

    void Init() {
        if (gWl.plasmaDesktops) {
            fBackend = kPlasma;
            org_kde_plasma_virtual_desktop_management_add_listener(gWl.plasmaDesktops, &kPlasmaVdMgmtListener, this);
        } else if (gWl.extWorkspaces) {
            fBackend = kExt;
            ext_workspace_manager_v1_add_listener(gWl.extWorkspaces, &kExtMgrListener, this);
        } else if (g_getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
            fBackend = kHyprland;
            InitHyprland();
        } else if (g_getenv("SWAYSOCK")) {
            fBackend = kSway;
            InitSway();
        }
        DebugLog("workspace backend: %s\n", BackendName());
    }

    const char* BackendName() const {
        switch (fBackend) {
            case kPlasma: return "KDE virtual desktops";
            case kExt: return "ext-workspace-v1";
            case kHyprland: return "Hyprland IPC";
            case kSway: return "Sway IPC";
            default: return "none";
        }
    }

    bool Available() const { return fBackend != kNone; }

    std::vector<WorkspaceInfo> List() const {
        std::vector<WorkspaceInfo> out;
        for (const auto& w : fWorkspaces) out.push_back(*w);
        std::stable_sort(out.begin(), out.end(), [](const WorkspaceInfo& a, const WorkspaceInfo& b) {
            return a.sortKey < b.sortKey;
        });
        return out;
    }

    int Count() const { return std::max<int>(1, fWorkspaces.size()); }

    int ActiveIndex() const {
        auto list = List();
        for (size_t i = 0; i < list.size(); ++i) if (list[i].active) return static_cast<int>(i);
        return 0;
    }

    std::string ActiveId() const {
        for (const auto& w : fWorkspaces) if (w->active) return w->id;
        return std::string();
    }

    void Activate(int index) {
        auto list = List();
        if (index < 0 || index >= static_cast<int>(list.size())) return;
        const WorkspaceInfo& w = list[index];
        switch (fBackend) {
            case kPlasma:
                org_kde_plasma_virtual_desktop_request_activate(static_cast<org_kde_plasma_virtual_desktop*>(w.handle));
                break;
            case kExt:
                ext_workspace_handle_v1_activate(static_cast<ext_workspace_handle_v1*>(w.handle));
                ext_workspace_manager_v1_commit(gWl.extWorkspaces);
                break;
            case kHyprland:
                HyprRequest("dispatch workspace " + w.id);
                RefreshHyprland();
                break;
            case kSway:
                SwayRequest(0, "workspace " + SwayQuote(w.name));
                RefreshSway();
                break;
            default:
                break;
        }
    }

private:
    enum Backend { kNone, kPlasma, kExt, kHyprland, kSway };

    void Changed() {
        if (fChangePending) return;
        fChangePending = true;
        RunLater([this]() {
            fChangePending = false;
            if (onChanged) onChanged();
        });
    }

    WorkspaceInfo* FindByHandle(void* h) {
        for (auto& w : fWorkspaces) if (w->handle == h) return w.get();
        return nullptr;
    }

    void RemoveByHandle(void* h) {
        fWorkspaces.erase(std::remove_if(fWorkspaces.begin(), fWorkspaces.end(),
            [h](const std::unique_ptr<WorkspaceInfo>& w) { return w->handle == h; }), fWorkspaces.end());
        Changed();
    }

    // ---- KDE virtual desktops ----
    static const org_kde_plasma_virtual_desktop_management_listener kPlasmaVdMgmtListener;
    static const org_kde_plasma_virtual_desktop_listener kPlasmaVdListener;

    // ---- ext-workspace-v1 ----
    static const ext_workspace_manager_v1_listener kExtMgrListener;
    static const ext_workspace_handle_v1_listener kExtWsListener;
    static const ext_workspace_group_handle_v1_listener kExtGroupListener;

    // ---- Hyprland IPC ----
    std::string HyprSocketPath(const char* name) const {
        const char* sig = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
        const char* runtime = g_getenv("XDG_RUNTIME_DIR");
        std::string modern = std::string(runtime ? runtime : "/tmp") + "/hypr/" + sig + "/" + name;
        if (FileExists(modern)) return modern;
        return std::string("/tmp/hypr/") + sig + "/" + name;
    }

    static int ConnectUnix(const std::string& path) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        g_strlcpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path));
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
        return fd;
    }

    std::string HyprRequest(const std::string& req) {
        int fd = ConnectUnix(HyprSocketPath(".socket.sock"));
        if (fd < 0) return std::string();
        if (write(fd, req.data(), req.size()) < 0) { close(fd); return std::string(); }
        std::string reply;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) reply.append(buf, n);
        close(fd);
        return reply;
    }

    void RefreshHyprland() {
        Json all = Json::Parse(HyprRequest("j/workspaces"));
        Json active = Json::Parse(HyprRequest("j/activeworkspace"));
        int activeId = static_cast<int>(active.Num("id", 1));
        // Hyprland creates workspaces on demand; show at least 1..4 (and
        // anything numbered beyond that which exists) as a fixed grid,
        // like Haiku's fixed workspace count.
        int maxId = 4;
        std::map<int, std::string> names;
        for (const auto& w : all.arr) {
            int id = static_cast<int>(w.Num("id", 0));
            if (id <= 0) continue; // special / scratchpad workspaces
            names[id] = w.Str("name");
            maxId = std::max(maxId, std::min(id, 10));
        }
        fWorkspaces.clear();
        for (int id = 1; id <= maxId; ++id) {
            auto w = std::make_unique<WorkspaceInfo>();
            w->id = std::to_string(id);
            w->name = names.count(id) ? names[id] : w->id;
            w->active = (id == activeId);
            w->sortKey = id;
            fWorkspaces.push_back(std::move(w));
        }
        Changed();
    }

    void InitHyprland() {
        RefreshHyprland();
        fEventFd = ConnectUnix(HyprSocketPath(".socket2.sock"));
        if (fEventFd < 0) return;
        g_unix_fd_add(fEventFd, G_IO_IN, [](int fd, GIOCondition, gpointer p) -> gboolean {
            auto* self = static_cast<WorkspaceManager*>(p);
            char buf[4096];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n <= 0) { close(fd); self->fEventFd = -1; return G_SOURCE_REMOVE; }
            std::string chunk(buf, n);
            if (chunk.find("workspace") != std::string::npos || chunk.find("focusedmon") != std::string::npos) {
                self->RefreshHyprland();
            }
            return G_SOURCE_CONTINUE;
        }, this);
    }

    // ---- Sway (i3) IPC ----
    static std::string SwayQuote(const std::string& s) {
        std::string q = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') q += '\\';
            q += c;
        }
        return q + "\"";
    }

    static bool SwayWrite(int fd, uint32_t type, const std::string& payload) {
        std::string msg = "i3-ipc";
        uint32_t len = payload.size();
        msg.append(reinterpret_cast<const char*>(&len), 4);
        msg.append(reinterpret_cast<const char*>(&type), 4);
        msg += payload;
        return write(fd, msg.data(), msg.size()) == static_cast<ssize_t>(msg.size());
    }

    static bool ReadExact(int fd, char* buf, size_t n) {
        size_t got = 0;
        while (got < n) {
            ssize_t r = read(fd, buf + got, n - got);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }

    static bool SwayRead(int fd, uint32_t* type, std::string* payload) {
        char header[14];
        if (!ReadExact(fd, header, 14)) return false;
        uint32_t len;
        memcpy(&len, header + 6, 4);
        memcpy(type, header + 10, 4);
        payload->resize(len);
        return len == 0 || ReadExact(fd, payload->data(), len);
    }

    std::string SwayRequest(uint32_t type, const std::string& payload) {
        const char* path = g_getenv("SWAYSOCK");
        int fd = path ? ConnectUnix(path) : -1;
        if (fd < 0) return std::string();
        std::string reply;
        uint32_t replyType = 0;
        if (SwayWrite(fd, type, payload)) SwayRead(fd, &replyType, &reply);
        close(fd);
        return reply;
    }

    void RefreshSway() {
        Json all = Json::Parse(SwayRequest(1, ""));
        fWorkspaces.clear();
        for (const auto& w : all.arr) {
            auto info = std::make_unique<WorkspaceInfo>();
            info->name = w.Str("name");
            info->id = info->name;
            info->active = w.Bool("focused") || w.Bool("visible");
            int num = static_cast<int>(w.Num("num", -1));
            info->sortKey = num >= 0 ? num : 1000 + static_cast<int>(fWorkspaces.size());
            fWorkspaces.push_back(std::move(info));
        }
        Changed();
    }

    void InitSway() {
        RefreshSway();
        const char* path = g_getenv("SWAYSOCK");
        fEventFd = path ? ConnectUnix(path) : -1;
        if (fEventFd < 0) return;
        SwayWrite(fEventFd, 2, "[\"workspace\"]");
        g_unix_fd_add(fEventFd, G_IO_IN, [](int fd, GIOCondition, gpointer p) -> gboolean {
            auto* self = static_cast<WorkspaceManager*>(p);
            uint32_t type = 0;
            std::string payload;
            if (!SwayRead(fd, &type, &payload)) { close(fd); self->fEventFd = -1; return G_SOURCE_REMOVE; }
            if (type & 0x80000000u) self->RefreshSway();
            return G_SOURCE_CONTINUE;
        }, this);
    }

    Backend fBackend = kNone;
    std::vector<std::unique_ptr<WorkspaceInfo>> fWorkspaces;
    int fEventFd = -1;
    bool fChangePending = false;
    int fCreationCounter = 0;
};

static WorkspaceManager gWorkspaces;

const org_kde_plasma_virtual_desktop_management_listener WorkspaceManager::kPlasmaVdMgmtListener = {
    .desktop_created = [](void* data, org_kde_plasma_virtual_desktop_management* mgmt, const char* id, uint32_t position) {
        auto* self = static_cast<WorkspaceManager*>(data);
        auto w = std::make_unique<WorkspaceInfo>();
        w->id = id ? id : "";
        w->name = w->id;
        w->sortKey = static_cast<int>(position);
        auto* desk = org_kde_plasma_virtual_desktop_management_get_virtual_desktop(mgmt, id);
        w->handle = desk;
        org_kde_plasma_virtual_desktop_add_listener(desk, &kPlasmaVdListener, self);
        // Later desktops shift existing positions up.
        for (auto& other : self->fWorkspaces) {
            if (other->sortKey >= w->sortKey) other->sortKey++;
        }
        self->fWorkspaces.push_back(std::move(w));
        self->Changed();
    },
    .desktop_removed = [](void* data, org_kde_plasma_virtual_desktop_management*, const char* id) {
        auto* self = static_cast<WorkspaceManager*>(data);
        for (auto& w : self->fWorkspaces) {
            if (w->id == (id ? id : "")) {
                void* h = w->handle;
                org_kde_plasma_virtual_desktop_destroy(static_cast<org_kde_plasma_virtual_desktop*>(h));
                self->RemoveByHandle(h);
                return;
            }
        }
    },
    .done = [](void* data, org_kde_plasma_virtual_desktop_management*) {
        static_cast<WorkspaceManager*>(data)->Changed();
    },
    .rows = [](void*, org_kde_plasma_virtual_desktop_management*, uint32_t) {},
};

const org_kde_plasma_virtual_desktop_listener WorkspaceManager::kPlasmaVdListener = {
    .desktop_id = [](void*, org_kde_plasma_virtual_desktop*, const char*) {},
    .name = [](void* data, org_kde_plasma_virtual_desktop* d, const char* name) {
        auto* self = static_cast<WorkspaceManager*>(data);
        if (WorkspaceInfo* w = self->FindByHandle(d)) w->name = name ? name : "";
    },
    .activated = [](void* data, org_kde_plasma_virtual_desktop* d) {
        auto* self = static_cast<WorkspaceManager*>(data);
        for (auto& w : self->fWorkspaces) w->active = (w->handle == d);
        self->Changed();
    },
    .deactivated = [](void* data, org_kde_plasma_virtual_desktop* d) {
        auto* self = static_cast<WorkspaceManager*>(data);
        if (WorkspaceInfo* w = self->FindByHandle(d)) w->active = false;
        self->Changed();
    },
    .done = [](void* data, org_kde_plasma_virtual_desktop*) {
        static_cast<WorkspaceManager*>(data)->Changed();
    },
    .removed = [](void* data, org_kde_plasma_virtual_desktop* d) {
        auto* self = static_cast<WorkspaceManager*>(data);
        if (self->FindByHandle(d)) {
            org_kde_plasma_virtual_desktop_destroy(d);
            self->RemoveByHandle(d);
        }
    },
    .position = [](void* data, org_kde_plasma_virtual_desktop* d, uint32_t index) {
        auto* self = static_cast<WorkspaceManager*>(data);
        if (WorkspaceInfo* w = self->FindByHandle(d)) w->sortKey = static_cast<int>(index);
    },
    .output_entered = [](void*, org_kde_plasma_virtual_desktop*, const char*) {},
};

const ext_workspace_manager_v1_listener WorkspaceManager::kExtMgrListener = {
    .workspace_group = [](void* data, ext_workspace_manager_v1*, ext_workspace_group_handle_v1* g) {
        ext_workspace_group_handle_v1_add_listener(g, &kExtGroupListener, data);
    },
    .workspace = [](void* data, ext_workspace_manager_v1*, ext_workspace_handle_v1* h) {
        auto* self = static_cast<WorkspaceManager*>(data);
        auto w = std::make_unique<WorkspaceInfo>();
        w->handle = h;
        w->sortKey = 100000 + self->fCreationCounter++;
        ext_workspace_handle_v1_add_listener(h, &kExtWsListener, self);
        self->fWorkspaces.push_back(std::move(w));
    },
    .done = [](void* data, ext_workspace_manager_v1*) {
        static_cast<WorkspaceManager*>(data)->Changed();
    },
    .finished = [](void*, ext_workspace_manager_v1*) {},
};

const ext_workspace_handle_v1_listener WorkspaceManager::kExtWsListener = {
    .id = [](void* data, ext_workspace_handle_v1* h, const char* id) {
        if (WorkspaceInfo* w = static_cast<WorkspaceManager*>(data)->FindByHandle(h)) w->id = id ? id : "";
    },
    .name = [](void* data, ext_workspace_handle_v1* h, const char* name) {
        if (WorkspaceInfo* w = static_cast<WorkspaceManager*>(data)->FindByHandle(h)) {
            w->name = name ? name : "";
            // Numeric names ("1", "2", ...) sort numerically.
            char* end = nullptr;
            long n = strtol(w->name.c_str(), &end, 10);
            if (end && *end == '\0' && !w->name.empty() && w->sortKey >= 100000) w->sortKey = static_cast<int>(n);
        }
    },
    .coordinates = [](void* data, ext_workspace_handle_v1* h, wl_array* coords) {
        WorkspaceInfo* w = static_cast<WorkspaceManager*>(data)->FindByHandle(h);
        if (w == nullptr || coords->size < sizeof(uint32_t)) return;
        uint32_t* c = static_cast<uint32_t*>(coords->data);
        uint32_t x = c[0], y = coords->size >= 2 * sizeof(uint32_t) ? c[1] : 0;
        w->sortKey = static_cast<int>(y * 1000 + x);
    },
    .state = [](void* data, ext_workspace_handle_v1* h, uint32_t state) {
        if (WorkspaceInfo* w = static_cast<WorkspaceManager*>(data)->FindByHandle(h)) {
            w->active = state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
        }
    },
    .capabilities = [](void*, ext_workspace_handle_v1*, uint32_t) {},
    .removed = [](void* data, ext_workspace_handle_v1* h) {
        auto* self = static_cast<WorkspaceManager*>(data);
        ext_workspace_handle_v1_destroy(h);
        self->RemoveByHandle(h);
    },
};

const ext_workspace_group_handle_v1_listener WorkspaceManager::kExtGroupListener = {
    .capabilities = [](void*, ext_workspace_group_handle_v1*, uint32_t) {},
    .output_enter = [](void*, ext_workspace_group_handle_v1*, wl_output*) {},
    .output_leave = [](void*, ext_workspace_group_handle_v1*, wl_output*) {},
    .workspace_enter = [](void*, ext_workspace_group_handle_v1*, ext_workspace_handle_v1*) {},
    .workspace_leave = [](void*, ext_workspace_group_handle_v1*, ext_workspace_handle_v1*) {},
    .removed = [](void*, ext_workspace_group_handle_v1* g) { ext_workspace_group_handle_v1_destroy(g); },
};

// =========================================================================
// VOLUME (PulseAudio API -- served by pipewire-pulse on CachyOS)
// =========================================================================
// Replaces the BMediaRoster master-gain parameter. Change notifications come
// from a PulseAudio subscription, so the dock never polls.
class VolumeControl {
public:
    std::function<void()> onChanged;
    float level = 0.5f;   // 0..1 of PA_VOLUME_NORM (overdrive clamps to 1 for display)
    bool muted = false;
    bool ready = false;

    void Init() {
        fMainloop = pa_glib_mainloop_new(nullptr);
        Connect();
    }

    void SetLevel(float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        level = v;
        if (!ready || fSink.empty() || fChannels == 0) return;
        pa_cvolume cv;
        pa_cvolume_set(&cv, fChannels, static_cast<pa_volume_t>(std::lround(v * PA_VOLUME_NORM)));
        pa_operation* op = pa_context_set_sink_volume_by_name(fContext, fSink.c_str(), &cv, nullptr, nullptr);
        if (op) pa_operation_unref(op);
        if (muted && v > 0.0f) SetMuted(false);
        if (onChanged) onChanged();
    }

    void SetMuted(bool m) {
        muted = m;
        if (!ready || fSink.empty()) return;
        pa_operation* op = pa_context_set_sink_mute_by_name(fContext, fSink.c_str(), m ? 1 : 0, nullptr, nullptr);
        if (op) pa_operation_unref(op);
        if (onChanged) onChanged();
    }

private:
    void Connect() {
        if (fContext) {
            pa_context_disconnect(fContext);
            pa_context_unref(fContext);
        }
        ready = false;
        fContext = pa_context_new(pa_glib_mainloop_get_api(fMainloop), "hDesktop");
        pa_context_set_state_callback(fContext, &StateCallback, this);
        pa_context_set_subscribe_callback(fContext, &SubscribeCallback, this);
        if (pa_context_connect(fContext, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) ScheduleReconnect();
    }

    void ScheduleReconnect() {
        if (fReconnectTimer) return;
        fReconnectTimer = g_timeout_add_seconds(3, [](gpointer p) -> gboolean {
            auto* self = static_cast<VolumeControl*>(p);
            self->fReconnectTimer = 0;
            self->Connect();
            return G_SOURCE_REMOVE;
        }, this);
    }

    void RefreshServer() {
        pa_operation* op = pa_context_get_server_info(fContext, [](pa_context* c, const pa_server_info* info, void* p) {
            auto* self = static_cast<VolumeControl*>(p);
            if (info == nullptr || info->default_sink_name == nullptr) return;
            self->fSink = info->default_sink_name;
            self->RefreshSink();
        }, this);
        if (op) pa_operation_unref(op);
    }

    void RefreshSink() {
        if (fSink.empty()) return;
        pa_operation* op = pa_context_get_sink_info_by_name(fContext, fSink.c_str(),
            [](pa_context*, const pa_sink_info* info, int eol, void* p) {
                auto* self = static_cast<VolumeControl*>(p);
                if (eol || info == nullptr) return;
                self->fSinkIndex = info->index;
                self->fChannels = info->volume.channels;
                self->level = std::clamp(static_cast<float>(pa_cvolume_avg(&info->volume)) / PA_VOLUME_NORM, 0.0f, 1.0f);
                self->muted = info->mute;
                if (self->onChanged) self->onChanged();
            }, this);
        if (op) pa_operation_unref(op);
    }

    static void StateCallback(pa_context* c, void* p) {
        auto* self = static_cast<VolumeControl*>(p);
        switch (pa_context_get_state(c)) {
            case PA_CONTEXT_READY: {
                self->ready = true;
                pa_operation* op = pa_context_subscribe(c,
                    static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER),
                    nullptr, nullptr);
                if (op) pa_operation_unref(op);
                self->RefreshServer();
                DebugLog("PulseAudio connected\n");
                break;
            }
            case PA_CONTEXT_FAILED:
            case PA_CONTEXT_TERMINATED:
                self->ready = false;
                self->ScheduleReconnect();
                break;
            default:
                break;
        }
    }

    static void SubscribeCallback(pa_context*, pa_subscription_event_type_t type, uint32_t index, void* p) {
        auto* self = static_cast<VolumeControl*>(p);
        unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SERVER) self->RefreshServer();
        else if (facility == PA_SUBSCRIPTION_EVENT_SINK && index == self->fSinkIndex) self->RefreshSink();
    }

    pa_glib_mainloop* fMainloop = nullptr;
    pa_context* fContext = nullptr;
    std::string fSink;
    uint32_t fSinkIndex = PA_INVALID_INDEX;
    uint8_t fChannels = 0;
    guint fReconnectTimer = 0;
};

static VolumeControl gVolume;

// =========================================================================
// SYSTEM TRAY (StatusNotifierItem + dbusmenu)
// =========================================================================
// Replaces probing Deskbar's replicant shelf. hDesktop acts as a
// StatusNotifierHost; if nobody else owns org.kde.StatusNotifierWatcher (e.g.
// a bare Hyprland session without waybar) it also becomes the watcher, and it
// stays queued for the name so it takes over if the other watcher exits.
struct TrayItem {
    std::string service, path;
    std::string id, title, status;
    std::string iconName, attentionIconName, iconThemePath, menuPath;
    bool itemIsMenu = false;
    IconRef pixmap, attentionPixmap;
    guint signalSub = 0;
    guint nameWatch = 0;
    guint refreshTimer = 0;
    bool loaded = false;
    // Drawing state owned by the dock.
    GLTexture texture;
    int textureKey = 0;
    uint64_t revision = 1;
    HRect rect;
};

static const char* kWatcherXml =
    "<node>"
    " <interface name='org.kde.StatusNotifierWatcher'>"
    "  <method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
    "  <method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>"
    "  <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "  <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "  <property name='ProtocolVersion' type='i' access='read'/>"
    "  <signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
    "  <signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
    "  <signal name='StatusNotifierHostRegistered'/>"
    "  <signal name='StatusNotifierHostUnregistered'/>"
    " </interface>"
    "</node>";

class SystemTray {
public:
    std::function<void()> onChanged;

    void Init() {
        GError* err = nullptr;
        fBus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
        if (fBus == nullptr) {
            WarnLog("no D-Bus session bus, system tray disabled: %s\n", err ? err->message : "?");
            g_clear_error(&err);
            return;
        }
        fHostName = "org.kde.StatusNotifierHost-" + std::to_string(getpid());
        g_bus_own_name_on_connection(fBus, fHostName.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr, nullptr, nullptr);

        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kWatcherXml, nullptr);
        static const GDBusInterfaceVTable vtable = {
            .method_call = &WatcherMethod,
            .get_property = &WatcherProperty,
            .set_property = nullptr,
            .padding = {nullptr},
        };
        fWatcherRegistration = g_dbus_connection_register_object(fBus, "/StatusNotifierWatcher",
            node->interfaces[0], &vtable, this, nullptr, nullptr);
        g_dbus_node_info_unref(node);

        g_bus_own_name_on_connection(fBus, "org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_NONE,
            [](GDBusConnection*, const char*, gpointer p) {
                auto* self = static_cast<SystemTray*>(p);
                DebugLog("tray: we are the StatusNotifierWatcher\n");
                self->fIsWatcher = true;
                self->DropExternalWatcher();
                g_dbus_connection_emit_signal(self->fBus, nullptr, "/StatusNotifierWatcher",
                    "org.kde.StatusNotifierWatcher", "StatusNotifierHostRegistered", nullptr, nullptr);
            },
            [](GDBusConnection*, const char*, gpointer p) {
                auto* self = static_cast<SystemTray*>(p);
                DebugLog("tray: using an existing StatusNotifierWatcher\n");
                self->fIsWatcher = false;
                self->UseExternalWatcher();
            },
            this, nullptr);
    }

    const std::vector<std::unique_ptr<TrayItem>>& Items() const { return fItems; }

    std::vector<TrayItem*> VisibleItems() const {
        std::vector<TrayItem*> out;
        for (const auto& it : fItems) {
            if (it->loaded && it->status != "Passive") out.push_back(it.get());
        }
        return out;
    }

    void Activate(TrayItem* it, int x, int y) { CallItem(it, "Activate", g_variant_new("(ii)", x, y)); }
    void SecondaryActivate(TrayItem* it, int x, int y) { CallItem(it, "SecondaryActivate", g_variant_new("(ii)", x, y)); }
    void ContextMenu(TrayItem* it, int x, int y) { CallItem(it, "ContextMenu", g_variant_new("(ii)", x, y)); }
    void Scroll(TrayItem* it, int delta) { CallItem(it, "Scroll", g_variant_new("(is)", delta, "vertical")); }

    bool HasMenu(const TrayItem* it) const { return !it->menuPath.empty() && it->menuPath != "/"; }

    // Fetches the item's dbusmenu layout and hands it back as MenuItems.
    void FetchMenu(TrayItem* it, std::function<void(std::vector<MenuItem>)> done) {
        if (!HasMenu(it)) { done({}); return; }
        struct Ctx {
            SystemTray* tray;
            std::string service, path;
            std::function<void(std::vector<MenuItem>)> done;
        };
        auto* ctx = new Ctx{this, it->service, it->menuPath, std::move(done)};
        // AboutToShow lets lazy menus (Electron, Qt) populate themselves first.
        g_dbus_connection_call(fBus, ctx->service.c_str(), ctx->path.c_str(), "com.canonical.dbusmenu",
            "AboutToShow", g_variant_new("(i)", 0), nullptr, G_DBUS_CALL_FLAGS_NONE, 500, nullptr,
            [](GObject*, GAsyncResult* res, gpointer p) {
                auto* ctx = static_cast<Ctx*>(p);
                GVariant* r = g_dbus_connection_call_finish(ctx->tray->fBus, res, nullptr);
                if (r) g_variant_unref(r);
                const char* props[] = {nullptr};
                g_dbus_connection_call(ctx->tray->fBus, ctx->service.c_str(), ctx->path.c_str(),
                    "com.canonical.dbusmenu", "GetLayout",
                    g_variant_new("(ii^as)", 0, -1, props), G_VARIANT_TYPE("(u(ia{sv}av))"),
                    G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
                    [](GObject*, GAsyncResult* res2, gpointer p2) {
                        auto* ctx = static_cast<Ctx*>(p2);
                        GError* err = nullptr;
                        GVariant* reply = g_dbus_connection_call_finish(ctx->tray->fBus, res2, &err);
                        std::vector<MenuItem> items;
                        if (reply) {
                            GVariant* layout = g_variant_get_child_value(reply, 1);
                            items = ctx->tray->ParseMenuChildren(layout, ctx->service, ctx->path);
                            g_variant_unref(layout);
                            g_variant_unref(reply);
                        } else {
                            DebugLog("tray: GetLayout failed: %s\n", err ? err->message : "?");
                            g_clear_error(&err);
                        }
                        ctx->done(std::move(items));
                        delete ctx;
                    }, ctx);
            }, ctx);
    }

private:
    // ---- watcher role ----
    static void WatcherMethod(GDBusConnection* conn, const char* sender, const char*, const char*,
        const char* method, GVariant* params, GDBusMethodInvocation* inv, gpointer p) {
        auto* self = static_cast<SystemTray*>(p);
        const char* arg = nullptr;
        g_variant_get(params, "(&s)", &arg);
        if (strcmp(method, "RegisterStatusNotifierItem") == 0) {
            std::string service, path;
            if (arg && arg[0] == '/') {
                service = sender;
                path = arg;
            } else {
                service = arg ? arg : sender;
                path = "/StatusNotifierItem";
            }
            std::string full = service + path;
            if (std::find(self->fRegistered.begin(), self->fRegistered.end(), full) == self->fRegistered.end()) {
                self->fRegistered.push_back(full);
                g_dbus_connection_emit_signal(conn, nullptr, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                    "StatusNotifierItemRegistered", g_variant_new("(s)", full.c_str()), nullptr);
            }
            self->AddItem(service, path);
        } else if (strcmp(method, "RegisterStatusNotifierHost") == 0) {
            g_dbus_connection_emit_signal(conn, nullptr, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                "StatusNotifierHostRegistered", nullptr, nullptr);
        }
        g_dbus_method_invocation_return_value(inv, nullptr);
    }

    static GVariant* WatcherProperty(GDBusConnection*, const char*, const char*, const char*,
        const char* prop, GError**, gpointer p) {
        auto* self = static_cast<SystemTray*>(p);
        if (strcmp(prop, "RegisteredStatusNotifierItems") == 0) {
            GVariantBuilder b;
            g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
            for (const auto& s : self->fRegistered) g_variant_builder_add(&b, "s", s.c_str());
            return g_variant_builder_end(&b);
        }
        if (strcmp(prop, "IsStatusNotifierHostRegistered") == 0) return g_variant_new_boolean(TRUE);
        if (strcmp(prop, "ProtocolVersion") == 0) return g_variant_new_int32(0);
        return nullptr;
    }

    // ---- host role against somebody else's watcher ----
    void UseExternalWatcher() {
        DropExternalWatcher();
        g_dbus_connection_call(fBus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
            "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierHost", g_variant_new("(s)", fHostName.c_str()),
            nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr, nullptr);
        auto onSignal = [](GDBusConnection*, const char*, const char*, const char*, const char* signal,
            GVariant* params, gpointer p) {
            auto* self = static_cast<SystemTray*>(p);
            const char* s = nullptr;
            g_variant_get(params, "(&s)", &s);
            if (s == nullptr) return;
            if (strcmp(signal, "StatusNotifierItemRegistered") == 0) self->AddFromWatcherString(s);
        };
        fExternalSub = g_dbus_connection_signal_subscribe(fBus, "org.kde.StatusNotifierWatcher",
            "org.kde.StatusNotifierWatcher", nullptr, "/StatusNotifierWatcher", nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, onSignal, this, nullptr);
        g_dbus_connection_call(fBus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr,
            [](GObject*, GAsyncResult* res, gpointer p) {
                auto* self = static_cast<SystemTray*>(p);
                GVariant* reply = g_dbus_connection_call_finish(self->fBus, res, nullptr);
                if (reply == nullptr) return;
                GVariant* v = nullptr;
                g_variant_get(reply, "(v)", &v);
                if (v && g_variant_is_of_type(v, G_VARIANT_TYPE("as"))) {
                    GVariantIter iter;
                    const char* s;
                    g_variant_iter_init(&iter, v);
                    while (g_variant_iter_next(&iter, "&s", &s)) self->AddFromWatcherString(s);
                }
                if (v) g_variant_unref(v);
                g_variant_unref(reply);
            }, this);
    }

    void DropExternalWatcher() {
        if (fExternalSub) g_dbus_connection_signal_unsubscribe(fBus, fExternalSub);
        fExternalSub = 0;
    }

    void AddFromWatcherString(const std::string& s) {
        size_t slash = s.find('/');
        if (slash == std::string::npos) AddItem(s, "/StatusNotifierItem");
        else AddItem(s.substr(0, slash), s.substr(slash));
    }

    // ---- items ----
    TrayItem* Find(const std::string& service, const std::string& path) {
        for (auto& it : fItems) if (it->service == service && it->path == path) return it.get();
        return nullptr;
    }

    void AddItem(const std::string& service, const std::string& path) {
        if (Find(service, path)) return;
        auto item = std::make_unique<TrayItem>();
        item->service = service;
        item->path = path;
        TrayItem* raw = item.get();
        fItems.push_back(std::move(item));
        DebugLog("tray: item %s%s\n", service.c_str(), path.c_str());

        raw->signalSub = g_dbus_connection_signal_subscribe(fBus, service.c_str(), "org.kde.StatusNotifierItem",
            nullptr, path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const char*, const char* objPath, const char*, const char*, GVariant*, gpointer p) {
                auto* self = static_cast<SystemTray*>(p);
                for (auto& it : self->fItems) {
                    if (it->path == objPath) self->ScheduleRefresh(it.get());
                }
            }, this, nullptr);
        struct WatchCtx { SystemTray* tray; std::string service, path; };
        raw->nameWatch = g_bus_watch_name_on_connection(fBus, service.c_str(), G_BUS_NAME_WATCHER_FLAGS_NONE,
            nullptr,
            [](GDBusConnection*, const char*, gpointer p) {
                auto* ctx = static_cast<WatchCtx*>(p);
                ctx->tray->RemoveItem(ctx->service, ctx->path);
            },
            new WatchCtx{this, service, path}, [](gpointer p) { delete static_cast<WatchCtx*>(p); });
        Refresh(raw);
    }

    void RemoveItem(const std::string& service, const std::string& path) {
        std::string full = service + path;
        auto reg = std::find(fRegistered.begin(), fRegistered.end(), full);
        if (reg != fRegistered.end()) {
            fRegistered.erase(reg);
            if (fIsWatcher) {
                g_dbus_connection_emit_signal(fBus, nullptr, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                    "StatusNotifierItemUnregistered", g_variant_new("(s)", full.c_str()), nullptr);
            }
        }
        for (auto it = fItems.begin(); it != fItems.end(); ++it) {
            if ((*it)->service == service && (*it)->path == path) {
                TrayItem* raw = it->get();
                if (raw->signalSub) g_dbus_connection_signal_unsubscribe(fBus, raw->signalSub);
                if (raw->refreshTimer) g_source_remove(raw->refreshTimer);
                guint watch = raw->nameWatch;
                DeleteTexture(raw->texture);
                fItems.erase(it);
                // Unwatching from inside the vanished callback is fine but
                // must happen after we're done touching the item.
                if (watch) RunLater([watch]() { g_bus_unwatch_name(watch); });
                Changed();
                return;
            }
        }
    }

    void ScheduleRefresh(TrayItem* it) {
        if (it->refreshTimer) return;
        struct Ctx { SystemTray* tray; std::string service, path; };
        it->refreshTimer = g_timeout_add_full(G_PRIORITY_DEFAULT, 100, [](gpointer p) -> gboolean {
            auto* ctx = static_cast<Ctx*>(p);
            if (TrayItem* item = ctx->tray->Find(ctx->service, ctx->path)) {
                item->refreshTimer = 0;
                ctx->tray->Refresh(item);
            }
            return G_SOURCE_REMOVE;
        }, new Ctx{this, it->service, it->path}, [](gpointer p) { delete static_cast<Ctx*>(p); });
    }

    void Refresh(TrayItem* it) {
        struct Ctx { SystemTray* tray; std::string service, path; };
        g_dbus_connection_call(fBus, it->service.c_str(), it->path.c_str(), "org.freedesktop.DBus.Properties",
            "GetAll", g_variant_new("(s)", "org.kde.StatusNotifierItem"), G_VARIANT_TYPE("(a{sv})"),
            G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
            [](GObject*, GAsyncResult* res, gpointer p) {
                auto* ctx = static_cast<Ctx*>(p);
                GError* err = nullptr;
                GVariant* reply = g_dbus_connection_call_finish(ctx->tray->fBus, res, &err);
                TrayItem* item = ctx->tray->Find(ctx->service, ctx->path);
                if (reply && item) {
                    GVariant* dict = g_variant_get_child_value(reply, 0);
                    ctx->tray->ApplyProperties(item, dict);
                    g_variant_unref(dict);
                } else if (err) {
                    DebugLog("tray: GetAll failed for %s: %s\n", ctx->service.c_str(), err->message);
                }
                g_clear_error(&err);
                if (reply) g_variant_unref(reply);
                delete ctx;
            }, new Ctx{this, it->service, it->path});
    }

    static IconRef DecodePixmap(GVariant* v) {
        // a(iiay), ARGB32 in network byte order; take the largest.
        if (v == nullptr || !g_variant_is_of_type(v, G_VARIANT_TYPE("a(iiay)"))) return nullptr;
        GVariantIter iter;
        g_variant_iter_init(&iter, v);
        int bestW = 0, bestH = 0;
        GVariant* bestData = nullptr;
        int w, h;
        GVariant* data;
        while (g_variant_iter_next(&iter, "(ii@ay)", &w, &h, &data)) {
            if (w * h > bestW * bestH && static_cast<int>(g_variant_get_size(data)) >= w * h * 4) {
                if (bestData) g_variant_unref(bestData);
                bestData = data;
                bestW = w;
                bestH = h;
            } else {
                g_variant_unref(data);
            }
        }
        if (bestData == nullptr) return nullptr;
        const uint8_t* src = static_cast<const uint8_t*>(g_variant_get_data(bestData));
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bestW, bestH);
        unsigned char* dst = cairo_image_surface_get_data(s);
        int stride = cairo_image_surface_get_stride(s);
        for (int y = 0; y < bestH; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(dst + y * stride);
            for (int x = 0; x < bestW; ++x) {
                const uint8_t* px = src + (y * bestW + x) * 4;
                uint32_t a = px[0], r = px[1], g = px[2], b = px[3];
                r = r * a / 255; g = g * a / 255; b = b * a / 255;
                row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(s);
        g_variant_unref(bestData);
        return MakeIconRef(s);
    }

    void ApplyProperties(TrayItem* it, GVariant* dict) {
        auto str = [&](const char* key) -> std::string {
            GVariant* v = g_variant_lookup_value(dict, key, nullptr);
            std::string r;
            if (v && (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH))) {
                r = g_variant_get_string(v, nullptr);
            }
            if (v) g_variant_unref(v);
            return r;
        };
        it->id = str("Id");
        it->title = str("Title");
        it->status = str("Status");
        it->iconName = str("IconName");
        it->attentionIconName = str("AttentionIconName");
        it->iconThemePath = str("IconThemePath");
        it->menuPath = str("Menu");
        GVariant* isMenu = g_variant_lookup_value(dict, "ItemIsMenu", G_VARIANT_TYPE_BOOLEAN);
        it->itemIsMenu = isMenu && g_variant_get_boolean(isMenu);
        if (isMenu) g_variant_unref(isMenu);
        GVariant* pix = g_variant_lookup_value(dict, "IconPixmap", nullptr);
        it->pixmap = DecodePixmap(pix);
        if (pix) g_variant_unref(pix);
        GVariant* apix = g_variant_lookup_value(dict, "AttentionIconPixmap", nullptr);
        it->attentionPixmap = DecodePixmap(apix);
        if (apix) g_variant_unref(apix);
        if (it->title.empty()) {
            // Tooltip title: (sa(iiay)ss) -- title is the third field.
            GVariant* tip = g_variant_lookup_value(dict, "ToolTip", nullptr);
            if (tip && g_variant_n_children(tip) >= 3) {
                GVariant* t = g_variant_get_child_value(tip, 2);
                if (g_variant_is_of_type(t, G_VARIANT_TYPE_STRING)) it->title = g_variant_get_string(t, nullptr);
                g_variant_unref(t);
            }
            if (tip) g_variant_unref(tip);
        }
        if (it->title.empty()) it->title = it->id;
        it->loaded = true;
        it->revision++;
        Changed();
    }

    void CallItem(TrayItem* it, const char* method, GVariant* params) {
        if (fBus == nullptr || it == nullptr) {
            if (params) g_variant_unref(g_variant_ref_sink(params));
            return;
        }
        g_dbus_connection_call(fBus, it->service.c_str(), it->path.c_str(), "org.kde.StatusNotifierItem", method,
            params, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr, nullptr);
    }

    static std::string StripMnemonic(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '_') {
                if (i + 1 < s.size() && s[i + 1] == '_') { out += '_'; ++i; }
                continue;
            }
            out += s[i];
        }
        return out;
    }

    std::vector<MenuItem> ParseMenuChildren(GVariant* layout, const std::string& service, const std::string& path) {
        std::vector<MenuItem> items;
        GVariant* children = g_variant_get_child_value(layout, 2);
        GVariantIter iter;
        g_variant_iter_init(&iter, children);
        GVariant* childBox;
        while ((childBox = g_variant_iter_next_value(&iter))) {
            GVariant* child = g_variant_get_variant(childBox);
            g_variant_unref(childBox);
            int id = 0;
            GVariant* props = nullptr;
            g_variant_get_child(child, 0, "i", &id);
            props = g_variant_get_child_value(child, 1);

            auto propStr = [&](const char* k) -> std::string {
                GVariant* v = g_variant_lookup_value(props, k, G_VARIANT_TYPE_STRING);
                std::string r = v ? g_variant_get_string(v, nullptr) : "";
                if (v) g_variant_unref(v);
                return r;
            };
            auto propBool = [&](const char* k, bool def) -> bool {
                GVariant* v = g_variant_lookup_value(props, k, G_VARIANT_TYPE_BOOLEAN);
                bool r = v ? g_variant_get_boolean(v) : def;
                if (v) g_variant_unref(v);
                return r;
            };
            auto propInt = [&](const char* k, int def) -> int {
                GVariant* v = g_variant_lookup_value(props, k, G_VARIANT_TYPE_INT32);
                int r = v ? g_variant_get_int32(v) : def;
                if (v) g_variant_unref(v);
                return r;
            };

            if (propBool("visible", true)) {
                MenuItem m;
                if (propStr("type") == "separator") {
                    m.separator = true;
                } else {
                    m.label = StripMnemonic(propStr("label"));
                    m.enabled = propBool("enabled", true);
                    std::string toggle = propStr("toggle-type");
                    int state = propInt("toggle-state", -1);
                    if (toggle == "checkmark") m.check = (state == 1) ? kCheckOn : kCheckOff;
                    else if (toggle == "radio") m.check = (state == 1) ? kRadioOn : kRadioOff;
                    m.iconName = propStr("icon-name");
                    GVariant* iconData = g_variant_lookup_value(props, "icon-data", G_VARIANT_TYPE("ay"));
                    if (iconData) {
                        m.icon = PngFromBytes(static_cast<const uint8_t*>(g_variant_get_data(iconData)),
                            g_variant_get_size(iconData));
                        g_variant_unref(iconData);
                    }
                    GVariant* grandChildren = g_variant_get_child_value(child, 2);
                    bool hasChildren = g_variant_n_children(grandChildren) > 0
                        || propStr("children-display") == "submenu";
                    g_variant_unref(grandChildren);
                    if (hasChildren) {
                        GVariant* keep = g_variant_ref(child);
                        auto sub = std::make_shared<std::vector<MenuItem>>(ParseMenuChildren(keep, service, path));
                        g_variant_unref(keep);
                        m.submenu = [sub]() { return *sub; };
                    } else {
                        GDBusConnection* bus = fBus;
                        m.action = [bus, service, path, id]() {
                            g_dbus_connection_call(bus, service.c_str(), path.c_str(), "com.canonical.dbusmenu", "Event",
                                g_variant_new("(isvu)", id, "clicked", g_variant_new_int32(0),
                                    static_cast<uint32_t>(time(nullptr))),
                                nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr, nullptr);
                        };
                    }
                }
                // Drop leading / doubled separators, which some apps emit.
                if (!(m.separator && (items.empty() || items.back().separator))) items.push_back(std::move(m));
            }
            g_variant_unref(props);
            g_variant_unref(child);
        }
        g_variant_unref(children);
        while (!items.empty() && items.back().separator) items.pop_back();
        return items;
    }

    static IconRef PngFromBytes(const uint8_t* data, size_t size) {
        struct Reader { const uint8_t* p; size_t left; };
        Reader r{data, size};
        cairo_surface_t* s = cairo_image_surface_create_from_png_stream(
            [](void* closure, unsigned char* out, unsigned int len) -> cairo_status_t {
                auto* rd = static_cast<Reader*>(closure);
                if (len > rd->left) return CAIRO_STATUS_READ_ERROR;
                memcpy(out, rd->p, len);
                rd->p += len;
                rd->left -= len;
                return CAIRO_STATUS_SUCCESS;
            }, &r);
        if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(s);
            return nullptr;
        }
        return MakeIconRef(s);
    }

    void Changed() {
        if (fChangePending) return;
        fChangePending = true;
        RunLater([this]() {
            fChangePending = false;
            if (onChanged) onChanged();
        });
    }

    GDBusConnection* fBus = nullptr;
    std::string fHostName;
    guint fWatcherRegistration = 0;
    guint fExternalSub = 0;
    bool fIsWatcher = false;
    std::vector<std::string> fRegistered;
    std::vector<std::unique_ptr<TrayItem>> fItems;
    bool fChangePending = false;
};

static SystemTray gTray;

// =========================================================================
// SYSTEM STATISTICS (/proc)
// =========================================================================
// get_system_info() / get_cpu_info() / team & thread info equivalents.
class CpuSampler {
public:
    std::vector<float> coreLoads;

    void Sample() {
        std::string stat = ReadFile("/proc/stat");
        std::vector<std::pair<uint64_t, uint64_t>> now; // (active, total)
        size_t pos = 0;
        while (pos < stat.size()) {
            size_t eol = stat.find('\n', pos);
            if (eol == std::string::npos) eol = stat.size();
            std::string line = stat.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.compare(0, 3, "cpu") != 0 || line.size() < 4 || !isdigit(static_cast<unsigned char>(line[3]))) continue;
            uint64_t v[10] = {0};
            sscanf(line.c_str(), "%*s %lu %lu %lu %lu %lu %lu %lu %lu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
            uint64_t idle = v[3] + v[4];
            uint64_t total = 0;
            for (int i = 0; i < 8; ++i) total += v[i];
            now.push_back({total - idle, total});
        }
        if (fPrev.size() == now.size()) {
            coreLoads.resize(now.size());
            for (size_t i = 0; i < now.size(); ++i) {
                uint64_t dt = now[i].second - fPrev[i].second;
                uint64_t da = now[i].first - fPrev[i].first;
                coreLoads[i] = dt > 0 ? std::clamp(static_cast<float>(da) / dt, 0.0f, 1.0f) : 0.0f;
            }
        }
        fPrev = now;
    }

    float Average() const {
        if (coreLoads.empty()) return 0.0f;
        float sum = 0;
        for (float f : coreLoads) sum += f;
        return sum / coreLoads.size();
    }

private:
    std::vector<std::pair<uint64_t, uint64_t>> fPrev;
};

struct MemInfo {
    uint64_t totalKb = 0, availableKb = 0;
    static MemInfo Read() {
        MemInfo m;
        std::string s = ReadFile("/proc/meminfo");
        auto field = [&](const char* name) -> uint64_t {
            size_t p = s.find(name);
            if (p == std::string::npos) return 0;
            return strtoull(s.c_str() + p + strlen(name), nullptr, 10);
        };
        m.totalKb = field("MemTotal:");
        m.availableKb = field("MemAvailable:");
        return m;
    }
};

struct ProcessInfo {
    pid_t pid = 0;
    std::string name;
    std::string exe;
    uid_t uid = 0;
    uint64_t cpuTicks = 0;
    double cpuPercent = 0;
};

class ProcessSampler {
public:
    // All processes, with CPU% measured since the previous call (the
    // BRealtimeCpuMenu pulse equivalent).
    std::vector<ProcessInfo> Sample() {
        std::vector<ProcessInfo> out;
        uint64_t nowMs = NowMs();
        double elapsedTicks = (fLastMs == 0) ? 0 : (nowMs - fLastMs) / 1000.0 * sysconf(_SC_CLK_TCK);
        long cpus = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
        std::map<pid_t, uint64_t> current;
        DIR* d = opendir("/proc");
        if (d == nullptr) return out;
        while (struct dirent* e = readdir(d)) {
            if (!isdigit(static_cast<unsigned char>(e->d_name[0]))) continue;
            ProcessInfo p;
            p.pid = atoi(e->d_name);
            std::string base = std::string("/proc/") + e->d_name;
            std::string stat = ReadFile(base + "/stat");
            size_t lp = stat.find('('), rp = stat.rfind(')');
            if (lp == std::string::npos || rp == std::string::npos) continue;
            p.name = stat.substr(lp + 1, rp - lp - 1);
            // Fields after ")": state(3) ... utime(14) stime(15)
            std::vector<std::string> fields;
            size_t pos = rp + 2;
            while (pos < stat.size() && fields.size() < 14) {
                size_t sp = stat.find(' ', pos);
                if (sp == std::string::npos) sp = stat.size();
                fields.push_back(stat.substr(pos, sp - pos));
                pos = sp + 1;
            }
            if (fields.size() < 13) continue;
            // Kernel threads have no memory map; skip them like Haiku skips "kernel".
            if (!FileExists(base + "/exe") && ReadFile(base + "/cmdline").empty()) continue;
            p.cpuTicks = strtoull(fields[11].c_str(), nullptr, 10) + strtoull(fields[12].c_str(), nullptr, 10);
            struct stat st;
            if (::stat(base.c_str(), &st) == 0) p.uid = st.st_uid;
            char exe[PATH_MAX];
            ssize_t n = readlink((base + "/exe").c_str(), exe, sizeof(exe) - 1);
            if (n > 0) { exe[n] = '\0'; p.exe = exe; }
            auto prev = fPrev.find(p.pid);
            if (prev != fPrev.end() && elapsedTicks > 0 && p.cpuTicks >= prev->second) {
                p.cpuPercent = (p.cpuTicks - prev->second) / (elapsedTicks * cpus) * 100.0;
            }
            current[p.pid] = p.cpuTicks;
            out.push_back(p);
        }
        closedir(d);
        fPrev = std::move(current);
        fLastMs = nowMs;
        return out;
    }

private:
    std::map<pid_t, uint64_t> fPrev;
    uint64_t fLastMs = 0;
};

// =========================================================================
// PANELS DEFINED LATER
// =========================================================================
static void ToggleAppDrawer();
static bool AppDrawerOpen();
static void CloseAppDrawer();
static void ShowConfigPanel();
static void ShowAlert(const std::string& title, const std::string& text, std::vector<std::string> buttons,
    std::function<void(int)> onChoice, int defaultButton = -1);
static void ShowAboutAlert();

// =========================================================================
// CLICK TARGETS FOR WIDGETS WITH NO SINGLE LINUX EQUIVALENT
// =========================================================================
static std::string FirstAvailable(std::initializer_list<std::pair<const char*, const char*>> candidates) {
    for (const auto& c : candidates) {
        if (HaveProgram(c.first)) return c.second;
    }
    return std::string();
}

// Haiku: /boot/system/preferences/Time
static std::string ClockCommand() {
    if (!gSettings.clockCommand.empty()) return gSettings.clockCommand;
    if (DesktopIs("KDE")) {
        std::string kde = FirstAvailable({{"systemsettings", "systemsettings kcm_clock"},
            {"kcmshell6", "kcmshell6 kcm_clock"}});
        if (!kde.empty()) return kde;
    }
    return FirstAvailable({{"gnome-calendar", "gnome-calendar"}, {"merkuro-calendar", "merkuro-calendar"},
        {"kalendar", "kalendar"}, {"gnome-clocks", "gnome-clocks"}, {"systemsettings", "systemsettings kcm_clock"},
        {"gnome-control-center", "gnome-control-center datetime"}});
}

// Haiku: /boot/system/preferences/Media
static std::string MixerCommand() {
    if (!gSettings.mixerCommand.empty()) return gSettings.mixerCommand;
    if (DesktopIs("KDE")) {
        std::string kde = FirstAvailable({{"systemsettings", "systemsettings kcm_pulseaudio"},
            {"kcmshell6", "kcmshell6 kcm_pulseaudio"}});
        if (!kde.empty()) return kde;
    }
    return FirstAvailable({{"pavucontrol", "pavucontrol"}, {"pwvucontrol", "pwvucontrol"},
        {"pavucontrol-qt", "pavucontrol-qt"}, {"systemsettings", "systemsettings kcm_pulseaudio"},
        {"gnome-control-center", "gnome-control-center sound"}});
}

// ---- Trash (freedesktop spec) -----------------------------------------------
static std::string TrashFilesDir() {
    return std::string(g_get_user_data_dir()) + "/Trash/files";
}

static bool TrashHasItems() {
    DIR* d = opendir(TrashFilesDir().c_str());
    if (d == nullptr) return false;
    bool any = false;
    while (struct dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) { any = true; break; }
    }
    closedir(d);
    return any;
}

static void OpenTrash() {
    OpenUri("trash:///");
}

// Removes everything inside `dir` (not `dir` itself), without following
// symlinks out of it.
static void RemoveDirectoryContents(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) names.push_back(e->d_name);
    }
    closedir(d);
    for (const auto& name : names) {
        std::string path = dir + "/" + name;
        struct stat st;
        if (lstat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            chmod(path.c_str(), st.st_mode | S_IRWXU); // trashed read-only folders
            RemoveDirectoryContents(path);
            rmdir(path.c_str());
        } else {
            unlink(path.c_str());
        }
    }
}

// Haiku: `trash --empty`. On Linux the trash is shared with the file
// manager, so prefer the desktop's own tool -- it keeps Dolphin's /
// Plasma's view of the trash in sync. `gio trash --empty` needs gvfs and
// silently does nothing without it (the usual case on KDE), so it's only
// used when gvfs is actually installed. Last resort: empty the
// freedesktop trash directory ourselves.
static void EmptyTrash() {
    for (const char* tool : {"ktrash6", "ktrash5"}) {
        if (HaveProgram(tool)) {
            RunDetached(std::string(tool) + " --empty");
            return;
        }
    }
    bool haveGvfs = FileExists("/usr/lib/gvfsd-trash") || FileExists("/usr/libexec/gvfsd-trash") ||
        FileExists("/usr/lib/gvfs/gvfsd-trash");
    if (haveGvfs && HaveProgram("gio")) {
        RunDetached("gio trash --empty");
        return;
    }
    std::string trash = std::string(g_get_user_data_dir()) + "/Trash";
    if (trash.size() <= strlen("/Trash")) return; // no data dir: never touch "/Trash"
    RemoveDirectoryContents(trash + "/files");
    RemoveDirectoryContents(trash + "/info");
    unlink((trash + "/directorysizes").c_str());
}

// ---- Power profile (Haiku: PowerStatus --toggle) ---------------------------
static std::string CurrentPowerProfile() {
    if (!HaveProgram("powerprofilesctl")) return std::string();
    char* out = nullptr;
    if (!g_spawn_command_line_sync("powerprofilesctl get", &out, nullptr, nullptr, nullptr)) return std::string();
    std::string r = Trim(out ? out : "");
    g_free(out);
    return r;
}

// =========================================================================
// DOCK ENGINE (HaikuGlDesktopEngine)
// =========================================================================
struct DockApp {
    std::string key;              // grouping key (lowercased app_id)
    std::string appId;
    AppEntry* entry = nullptr;
    std::string displayName;
    std::vector<Toplevel*> windows;
    GLTexture icon;
    int iconKey = 0;
    bool minimized = false;       // dimmed: nothing of it visible on this workspace
    bool foreground = false;
    bool closing = false;         // gone, kept only for the close effect
    HRect bounds;
    HRect lastIconRectSent;
};

enum SlotKind { kSlotLeaf, kSlotApp, kSlotTrash, kSlotTray, kSlotClock, kSlotVolume, kSlotCpu, kSlotWorkspaces };

struct DockSlot {
    SlotKind kind;
    int appIndex = -1;
    float width = 0;     // horizontal footprint
    float scale = 1.0f;  // hover magnification
    HRect bounds;        // drawn rect (icons: square; widgets: their own box)
};

struct DockLayout {
    HRect plate;
    float maxDockHeight = 48;
    float totalWidth = 0;
    float appSeparatorX = -1;   // divider between launchers and running apps
    float widgetDividerX = -1;  // divider before the trash (Haiku: "left clock divider")
    std::vector<DockSlot> slots;
    const DockSlot* Find(SlotKind k) const {
        for (const auto& s : slots) if (s.kind == k) return &s;
        return nullptr;
    }
};

enum AutoHideState { STATE_VISIBLE, STATE_HIDING, STATE_HIDDEN, STATE_SHOWING };

class DockEngine : public Surface {
public:
    zwlr_layer_surface_v1* layerSurface = nullptr;

    bool Create() {
        CreateSurface();
        gActivationSurface = surface;
        Output* out = FindOutput(gSettings.output);
        layerSurface = zwlr_layer_shell_v1_get_layer_surface(gWl.layerShell, surface,
            out ? out->output : nullptr, LayerForSettings(), "hdesktop-dock");
        zwlr_layer_surface_v1_add_listener(layerSurface, &kLayerListener, this);
        zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
        ApplyGeometrySettings();
        wl_surface_commit(surface);

        gToplevels.onChanged = [this]() { SyncApps(); RequestRender(); };
        gWorkspaces.onChanged = [this]() { SyncApps(); RequestRender(); };
        gVolume.onChanged = [this]() { RequestRender(); };
        gTray.onChanged = [this]() { RequestRender(); };
        gMenus.onStateChanged = [this]() { RequestRender(); };

        // Metrics pulse: CPU graph, clock, trash state (Haiku: 1s metrics +
        // 100ms CPU sampling inside RenderFrame).
        g_timeout_add(250, [](gpointer p) -> gboolean {
            auto* self = static_cast<DockEngine*>(p);
            self->fCpu.Sample();
            if (self->fCpuHistory.size() != self->fCpu.coreLoads.size()) {
                self->fCpuHistory.assign(self->fCpu.coreLoads.size(), 0.0f);
            }
            uint64_t now = NowMs();
            if (now - self->fLastTrashCheck >= 1000) {
                self->fLastTrashCheck = now;
                bool full = TrashHasItems();
                if (full != self->fTrashFull) {
                    self->fTrashFull = full;
                    DeleteTexture(self->fTrashIcon);
                }
            }
            self->RequestRender();
            return G_SOURCE_CONTINUE;
        }, this);
        return true;
    }

    // Re-applies everything the settings panel can change.
    void SettingsChanged() {
        if (layerSurface == nullptr) return;
        if (gWl.layerShellVersion >= 2) zwlr_layer_surface_v1_set_layer(layerSurface, LayerForSettings());
        ApplyGeometrySettings();
        wl_surface_commit(surface);
        if (!gSettings.autoHide) {
            fDockState = STATE_VISIBLE;
            fTargetY = 0;
        }
        InvalidateTextures();
        gMenus.CloseHover();
        RequestRender();
    }

    void RequestRender() {
        fDirty = true;
        if (!fConfigured || fFrameCallback || fRenderIdle) return;
        fRenderIdle = g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer p) -> gboolean {
            auto* self = static_cast<DockEngine*>(p);
            self->fRenderIdle = 0;
            self->RenderNow();
            return G_SOURCE_REMOVE;
        }, this, nullptr);
    }

    // ---- Surface input ----------------------------------------------------
    void PointerEnter(double x, double y) override {
        fPointerInside = true;
        PointerMotion(x, y);
    }

    void PointerMotion(double x, double y) override {
        fMouseX = static_cast<float>(x);
        fMouseY = static_cast<float>(y);
        fPointerInside = true;
        if (fVolumePressed) {
            if (std::abs(fMouseX - fVolumePressX) >= 4.0f) fVolumeDragging = true;
            if (fVolumeDragging && fVolumeRect.Width() > 0) {
                gVolume.SetLevel((fMouseX - fVolumeRect.left) / fVolumeRect.Width());
            }
        }
        RequestRender();
    }

    void PointerLeave() override {
        fPointerInside = false;
        fVolumePressed = false;
        fVolumeDragging = false;
        RequestRender();
    }

    void PointerButton(int button, bool pressed, uint32_t serial) override {
        if (!pressed) {
            if (button == kButtonLeft && fVolumePressed) {
                if (!fVolumeDragging) LaunchCommand(MixerCommand());
                fVolumePressed = false;
                fVolumeDragging = false;
            }
            return;
        }
        DebugLog("dock: button %d at %.0f,%.0f\n", button, fMouseX, fMouseY);
        HandleMouseClick(fMouseX, fMouseY, button, serial);
        RequestRender();
    }

    void PointerAxis(double dy) override { HandleMouseWheel(static_cast<int>(dy)); }

    void ScaleChanged() override {
        ResizeEglWindow();
        InvalidateTextures();
        RequestRender();
    }

    // ---- Launch / close effects (fEffectAppTeam / fClosingAppTeam) ---------
    // The .desktop database was reloaded: every AppEntry pointer is stale.
    void AppsReloaded() {
        for (auto& app : fApps) app.entry = app.closing ? nullptr : gApps->Resolve(app.appId);
        SyncApps();
        RequestRender();
    }

    bool PointerInsideDock() const { return fPointerInside; }

    void TriggerEffect(const std::string& key) {
        fEffectKey = key;
        fEffectStart = NowMs();
        RequestRender();
    }

private:
    // =====================================================================
    // SURFACE / EGL PLUMBING
    // =====================================================================
    static uint32_t LayerForSettings() {
        return gSettings.keepAboveWindows ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    }

    // Haiku: targetWindowHeight = ceil(iconSize * 2 + 50), full screen width,
    // pinned to the chosen screen edge.
    int PanelHeight() const { return static_cast<int>(std::ceil(gSettings.baseIconSize * 2.0f + 50.0f)); }

    void ApplyGeometrySettings() {
        bool top = gSettings.dockLocation == kDockLocationTop;
        uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
            (top ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
        zwlr_layer_surface_v1_set_anchor(layerSurface, anchor);
        zwlr_layer_surface_v1_set_size(layerSurface, 0, PanelHeight());
        // Resting plate height + its 15px margin from the screen edge.
        int zone = (gSettings.reserveSpace && !gSettings.autoHide)
            ? static_cast<int>(std::ceil(gSettings.baseIconSize + 20.0f + 15.0f)) : 0;
        zwlr_layer_surface_v1_set_exclusive_zone(layerSurface, zone);
    }

    void ResizeEglWindow() {
        if (!fConfigured || fWidth <= 0 || fHeight <= 0) return;
        int pw, ph;
        if (UsesFractional()) {
            pw = static_cast<int>(std::ceil(fWidth * scale));
            ph = static_cast<int>(std::ceil(fHeight * scale));
            wp_viewport_set_destination(viewport, fWidth, fHeight);
            wl_surface_set_buffer_scale(surface, 1);
        } else {
            pw = fWidth * intScale;
            ph = fHeight * intScale;
            wl_surface_set_buffer_scale(surface, intScale);
        }
        fPixelW = pw;
        fPixelH = ph;
        if (fEglWindow == nullptr) {
            fEglWindow = wl_egl_window_create(surface, pw, ph);
            fEglSurface = eglCreateWindowSurface(gEgl.display, gEgl.config,
                reinterpret_cast<EGLNativeWindowType>(fEglWindow), nullptr);
            eglMakeCurrent(gEgl.display, fEglSurface, fEglSurface, gEgl.context);
            // Frame pacing comes from wl_surface.frame callbacks; a blocking
            // swap interval would stall the whole main loop while hidden.
            eglSwapInterval(gEgl.display, 0);
        } else {
            wl_egl_window_resize(fEglWindow, pw, ph, 0, 0);
        }
    }

    static const zwlr_layer_surface_v1_listener kLayerListener;
    static const wl_callback_listener kFrameListener;

    void Configure(uint32_t serial, uint32_t w, uint32_t h) {
        zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
        bool sizeChanged = (static_cast<int>(w) != fWidth || static_cast<int>(h) != fHeight);
        if (w > 0) fWidth = static_cast<int>(w);
        if (h > 0) fHeight = static_cast<int>(h);
        fConfigured = true;
        if (sizeChanged || fEglWindow == nullptr) ResizeEglWindow();
        RequestRender();
    }

    // =====================================================================
    // TASKBAR SYNC (SyncDockWithRunningDeskbarApps)
    // =====================================================================
    static std::string GroupKeyFor(const Toplevel* t) {
        if (!t->appId.empty()) return ToLower(t->appId);
        if (t->pid) return "pid:" + std::to_string(t->pid);
        return "title:" + t->title;
    }

    void SyncApps() {
        std::vector<Toplevel*> windows = gToplevels.Windows();
        std::string desk = gWorkspaces.ActiveId();
        std::vector<DockApp> next;
        auto findIn = [](std::vector<DockApp>& list, const std::string& key) -> DockApp* {
            for (auto& a : list) if (a.key == key) return &a;
            return nullptr;
        };

        // Keep the existing order; append newcomers in first-seen order.
        for (auto& old : fApps) {
            if (old.closing) continue;
            DockApp copy = old;
            copy.windows.clear();
            next.push_back(copy);
        }
        for (Toplevel* t : windows) {
            std::string key = GroupKeyFor(t);
            DockApp* app = findIn(next, key);
            if (app == nullptr) {
                DockApp fresh;
                fresh.key = key;
                fresh.appId = t->appId;
                next.push_back(fresh);
                app = &next.back();
                if (fInitialSyncDone && gSettings.openEffect != kEffectNone) TriggerEffect("app:" + key);
            }
            app->windows.push_back(t);
        }

        // Drop apps that lost every window -- optionally keeping them for the
        // close effect, like the Haiku build re-appends the closing team.
        std::vector<DockApp> result;
        uint64_t now = NowMs();
        for (auto& app : next) {
            if (!app.windows.empty()) {
                result.push_back(app);
                continue;
            }
            if (gSettings.closeEffect != kEffectNone && fInitialSyncDone && fClosingKey.empty()) {
                app.closing = true;
                fClosingKey = app.key;
                fCloseStart = now;
                result.push_back(app);
            } else {
                DeleteTexture(app.icon);
            }
        }
        // A closing app still animating stays until its effect ends.
        for (auto& old : fApps) {
            if (old.closing && old.key == fClosingKey && now - fCloseStart < static_cast<uint64_t>(gSettings.effectDurationMs)) {
                if (findIn(result, old.key) == nullptr) result.push_back(old);
            } else if (old.closing && findIn(result, old.key) == nullptr) {
                DeleteTexture(old.icon);
            }
        }

        for (auto& app : result) {
            if (app.closing) continue;
            if (!app.windows.empty()) {
                if (app.appId.empty()) app.appId = app.windows.front()->appId;
                app.entry = gApps->Resolve(app.appId);
                app.displayName = app.entry ? app.entry->name
                    : (!app.appId.empty() ? app.appId : app.windows.front()->title);
            }
            bool anyVisible = false;
            bool anyActive = false;
            for (Toplevel* t : app.windows) {
                if (t->activated) anyActive = true;
                if (!t->minimized && gToplevels.IsOnDesktop(t, desk)) anyVisible = true;
            }
            app.foreground = anyActive;
            // Haiku: minimized == no normal visible windows on this workspace,
            // unless the app is the foreground one.
            app.minimized = !anyActive && !anyVisible;
        }
        fApps = std::move(result);
        fInitialSyncDone = true;
        RefreshHoverPopup();
    }

    // =====================================================================
    // TEXTURES
    // =====================================================================
    int IconPixels(float logical) const {
        // Rasterized at the full 1.8x hover size so zooming stays crisp.
        return std::max(8, static_cast<int>(std::ceil(logical * 1.8f * scale)));
    }

    void InvalidateTextures() {
        for (auto& a : fApps) DeleteTexture(a.icon);
        DeleteTexture(fLeafIcon);
        DeleteTexture(fTrashIcon);
        DeleteTexture(fClockTexture);
        fLastClockString.clear();
        for (const auto& t : gTray.Items()) DeleteTexture(t->texture);
        for (auto& kv : fLabelCache) DeleteTexture(kv.second);
        fLabelCache.clear();
    }

    GLTexture LoadIconTexture(const std::vector<std::string>& names, float logicalSize, const std::string& label) {
        int px = IconPixels(logicalSize);
        cairo_surface_t* s = nullptr;
        for (const auto& n : names) {
            if (n.empty()) continue;
            s = gIconTheme->Load(n, px);
            if (s) break;
        }
        if (s == nullptr) s = MakePlaceholderIcon(px, label);
        GLTexture t = UploadTexture(s, logicalSize, logicalSize);
        cairo_surface_destroy(s);
        return t;
    }

    void EnsureAppIcon(DockApp& app) {
        int key = IconPixels(gSettings.baseIconSize);
        if (app.icon.id != 0 && app.iconKey == key) return;
        DeleteTexture(app.icon);
        std::vector<std::string> names;
        for (Toplevel* t : app.windows) if (!t->themedIcon.empty()) { names.push_back(t->themedIcon); break; }
        if (app.entry) names.push_back(app.entry->iconName);
        names.push_back(app.appId);
        names.push_back(ToLower(app.appId));
        size_t dot = app.appId.rfind('.');
        if (dot != std::string::npos) names.push_back(ToLower(app.appId.substr(dot + 1)));
        app.icon = LoadIconTexture(names, gSettings.baseIconSize, app.displayName);
        app.iconKey = key;
    }

    // The Haiku leaf (AboutSystem's icon) becomes the distribution's logo.
    void EnsureLeafIcon() {
        if (fLeafIcon.id != 0) return;
        std::vector<std::string> names;
        std::string osRelease = ReadFile("/etc/os-release");
        std::string distroId;
        size_t p = osRelease.find("\nID=");
        if (osRelease.compare(0, 3, "ID=") == 0) p = 0; else if (p != std::string::npos) p += 1;
        if (p != std::string::npos) {
            size_t eol = osRelease.find('\n', p);
            distroId = Trim(osRelease.substr(p + 3, eol == std::string::npos ? std::string::npos : eol - p - 3));
            distroId.erase(std::remove(distroId.begin(), distroId.end(), '"'), distroId.end());
        }
        size_t logoPos = osRelease.find("LOGO=");
        if (logoPos != std::string::npos) {
            size_t eol = osRelease.find('\n', logoPos);
            std::string logo = Trim(osRelease.substr(logoPos + 5, eol == std::string::npos ? std::string::npos : eol - logoPos - 5));
            logo.erase(std::remove(logo.begin(), logo.end(), '"'), logo.end());
            names.push_back(logo);
        }
        if (!distroId.empty()) {
            names.push_back("distributor-logo-" + distroId);
            names.push_back(distroId + "-logo");
            names.push_back(distroId);
        }
        for (const char* n : {"start-here", "distributor-logo", "start-here-kde", "view-app-grid", "applications-all"}) {
            names.push_back(n);
        }
        fLeafIcon = LoadIconTexture(names, gSettings.baseIconSize, "H");
    }

    void EnsureTrashIcon() {
        if (fTrashIcon.id != 0) return;
        std::vector<std::string> names = fTrashFull
            ? std::vector<std::string>{"user-trash-full", "trashcan_full", "user-trash"}
            : std::vector<std::string>{"user-trash", "trashcan_empty"};
        fTrashIcon = LoadIconTexture(names, gSettings.baseIconSize, "T");
    }

    void EnsureTrayTexture(TrayItem* it, float logicalSize) {
        int px = std::max(8, static_cast<int>(std::ceil(logicalSize * 1.8f * scale)));
        int key = px * 1000003 + static_cast<int>(it->revision % 1000003);
        if (it->texture.id != 0 && it->textureKey == key) return;
        DeleteTexture(it->texture);
        bool attention = (it->status == "NeedsAttention");
        cairo_surface_t* s = nullptr;
        std::string name = attention && !it->attentionIconName.empty() ? it->attentionIconName : it->iconName;
        if (!name.empty()) s = gIconTheme->Load(name, px, it->iconThemePath);
        IconRef pix = attention && it->attentionPixmap ? it->attentionPixmap : it->pixmap;
        if (s == nullptr && pix) {
            s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
            cairo_t* cr = cairo_create(s);
            IconTheme::PaintScaled(cr, pix.get(), px);
            cairo_destroy(cr);
        }
        if (s == nullptr) s = MakePlaceholderIcon(px, it->title);
        it->texture = UploadTexture(s, logicalSize, logicalSize);
        it->textureKey = key;
        cairo_surface_destroy(s);
    }

    // Hover titles / date tooltip capsules, cached by string.
    const GLTexture& CapsuleLabel(const std::string& text) {
        auto it = fLabelCache.find(text);
        if (it != fLabelCache.end()) return it->second;
        if (fLabelCache.size() > 64) {
            for (auto& kv : fLabelCache) DeleteTexture(kv.second);
            fLabelCache.clear();
        }
        Image img = RenderCapsuleTextImage(text, 11.0, scale);
        GLTexture t = UploadImage(img);
        FreeImage(img);
        return fLabelCache[text] = t;
    }

    // UpdateLiveClockTexture(): rasterized big (32px) and drawn at 0.42x,
    // exactly like the Haiku build, re-rendered once a minute.
    void UpdateClockTexture() {
        time_t raw = time(nullptr);
        struct tm* ti = localtime(&raw);
        if (ti == nullptr) return;
        char buf[32];
        strftime(buf, sizeof(buf), gSettings.clock24h ? "%H:%M" : "%I:%M %p", ti);
        std::string str(buf);
        if (!gSettings.clock24h && !str.empty() && str[0] == '0') str.erase(0, 1);
        bool lightText = gSettings.dockAlpha < 0.35f;
        std::string key = str + (lightText ? "#w" : "#b");
        if (key == fLastClockString && fClockTexture.id != 0) return;
        DeleteTexture(fClockTexture);
        fLastClockString = key;
        RGBA color = lightText ? RGBA{1, 1, 1, 1} : RGBA{0, 0, 0, 1};
        Image img = RenderTextImage(str, 32.0, false, color, scale * 0.42 * 1.8 * (gSettings.baseIconSize / 48.0f));
        fClockTexture = UploadImage(img);
        FreeImage(img);
    }

    // =====================================================================
    // LAYOUT (PASS 1 + PASS 2 of RenderFrame / HandleMouseClick)
    // =====================================================================
    // The Haiku build computes this twice, once for drawing and once for hit
    // testing; here both use the same function so they can't drift apart.
    float MagnifyScale(float centerX, float centerY) const {
        if (!fHoverActive) return 1.0f;
        float dx = fLayoutMouseX - centerX;
        float dy = fLayoutMouseY - centerY;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d >= 180.0f) return 1.0f;
        float ratio = d / 180.0f;
        return 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
    }

    float DockEdgeY(float insetFromEdge) const {
        return (gSettings.dockLocation == kDockLocationTop) ? insetFromEdge : (fHeight - insetFromEdge);
    }

    float TrayIconBase() const { return 18.0f * (gSettings.baseIconSize / 48.0f); }
    float ClockBaseWidth() const {
        return fClockTexture.id ? static_cast<float>(fClockTexture.logicalW) * 0.42f * (gSettings.baseIconSize / 48.0f) : 0.0f;
    }
    bool ShowWorkspaces() const { return gSettings.workspaceSwitcher && gWorkspaces.Available(); }

    DockLayout ComputeLayout() {
        DockLayout L;
        const float baseSize = gSettings.baseIconSize;
        const float padding = 12.0f;
        const float clockSectionPadding = 24.0f;
        const float separatorGapPadding = 16.0f;
        const float ratio = baseSize / 48.0f;
        const float iconCenterY = DockEdgeY(10.0f + baseSize / 2.0f);
        const size_t appCount = fApps.size();
        const auto trayItems = gSettings.showSystemTray ? gTray.VisibleItems() : std::vector<TrayItem*>();
        const float trayIcon = TrayIconBase();
        const float baselineTrayWidth = trayItems.empty() ? 0.0f
            : trayItems.size() * trayIcon + (trayItems.size() - 1) * 6.0f * ratio;

        float total = 0.0f;
        std::vector<DockSlot> slots;
        for (int pass = 0; pass < 3; ++pass) {
            slots.clear();
            L.maxDockHeight = baseSize;
            float x = fWidth / 2.0f - total / 2.0f;
            const float left = x;

            auto addSquare = [&](SlotKind kind, int appIndex) {
                float scale = MagnifyScale(x + baseSize / 2.0f, iconCenterY);
                DockSlot s;
                s.kind = kind;
                s.appIndex = appIndex;
                s.scale = scale;
                s.width = baseSize * scale;
                L.maxDockHeight = std::max(L.maxDockHeight, s.width);
                slots.push_back(s);
                x += s.width;
            };
            auto addWidget = [&](SlotKind kind, float baseWidth) {
                float scale = MagnifyScale(x + baseWidth / 2.0f, iconCenterY);
                DockSlot s;
                s.kind = kind;
                s.scale = scale;
                s.width = baseWidth * scale;
                slots.push_back(s);
                x += s.width;
            };

            // 1. Leaf launcher + running applications
            addSquare(kSlotLeaf, -1);
            if (appCount > 0) x += padding + separatorGapPadding;
            for (size_t i = 0; i < appCount; ++i) {
                addSquare(kSlotApp, static_cast<int>(i));
                if (i + 1 < appCount) x += padding;
            }
            // 2. Trash
            x += clockSectionPadding;
            addSquare(kSlotTrash, -1);
            // 3. System tray
            if (baselineTrayWidth > 0) {
                x += clockSectionPadding;
                addWidget(kSlotTray, baselineTrayWidth);
            }
            // 4. Clock
            if (fClockTexture.id != 0) {
                x += clockSectionPadding;
                addWidget(kSlotClock, ClockBaseWidth());
            }
            // 5. Volume
            x += clockSectionPadding * ratio;
            addWidget(kSlotVolume, 44.0f * ratio);
            // 6. CPU graph
            x += clockSectionPadding * ratio;
            addWidget(kSlotCpu, 60.0f * ratio);
            // 7. Workspace switcher
            if (ShowWorkspaces()) {
                x += clockSectionPadding * ratio;
                addWidget(kSlotWorkspaces, 60.0f * ratio);
            }
            total = x - left;
        }

        // PASS 2: settle bounds
        const float dockMargin = 15.0f;
        const float sidePadding = 20.0f;
        L.totalWidth = total;
        L.plate.left = fWidth / 2.0f - total / 2.0f - sidePadding;
        L.plate.right = fWidth / 2.0f + total / 2.0f + sidePadding;
        if (gSettings.dockLocation == kDockLocationTop) {
            L.plate.top = dockMargin;
            L.plate.bottom = L.plate.top + L.maxDockHeight + 20.0f;
        } else {
            L.plate.bottom = fHeight - dockMargin;
            L.plate.top = L.plate.bottom - L.maxDockHeight - 20.0f;
        }

        const bool top = gSettings.dockLocation == kDockLocationTop;
        const float widgetCenterY = top ? (L.plate.top + 10.0f + L.maxDockHeight / 2.0f)
                                        : (L.plate.bottom - 10.0f - L.maxDockHeight / 2.0f);
        float x = L.plate.left + sidePadding;
        for (size_t i = 0; i < slots.size(); ++i) {
            DockSlot& s = slots[i];
            // Re-apply the same gaps pass 1 used.
            if (s.kind == kSlotApp) {
                if (s.appIndex == 0) {
                    L.appSeparatorX = x + (padding + separatorGapPadding) / 2.0f;
                    x += padding + separatorGapPadding;
                } else {
                    x += padding;
                }
            } else if (s.kind == kSlotTrash) {
                L.widgetDividerX = x + clockSectionPadding / 2.0f;
                x += clockSectionPadding;
            } else if (s.kind == kSlotTray || s.kind == kSlotClock) {
                x += clockSectionPadding;
            } else if (s.kind == kSlotVolume || s.kind == kSlotCpu || s.kind == kSlotWorkspaces) {
                x += clockSectionPadding * ratio;
            }

            if (s.kind == kSlotLeaf || s.kind == kSlotApp || s.kind == kSlotTrash) {
                s.bounds.left = x;
                s.bounds.right = x + s.width;
                if (top) {
                    s.bounds.top = L.plate.top + 10.0f;
                    s.bounds.bottom = s.bounds.top + s.width;
                } else {
                    s.bounds.bottom = L.plate.bottom - 10.0f;
                    s.bounds.top = s.bounds.bottom - s.width;
                }
            } else {
                float h;
                switch (s.kind) {
                    case kSlotTray: h = trayIcon * s.scale; break;
                    case kSlotClock: h = static_cast<float>(fClockTexture.logicalH) * 0.42f * ratio * s.scale; break;
                    case kSlotVolume: h = 12.0f * ratio * s.scale; break;
                    default: h = 28.0f * ratio * s.scale; break;
                }
                s.bounds.left = x;
                s.bounds.right = x + s.width;
                s.bounds.top = widgetCenterY - h / 2.0f;
                s.bounds.bottom = widgetCenterY + h / 2.0f;
            }
            x += s.width;
        }
        L.slots = std::move(slots);
        return L;
    }

    // Autohide slides the whole dock toward its screen edge.
    float DirectionalOffset() const {
        return gSettings.dockLocation == kDockLocationTop ? -fCurrentY : fCurrentY;
    }

    // =====================================================================
    // RENDERING (RenderFrame)
    // =====================================================================
    struct Effect {
        float bounce = 0, rotation = 0, scaleX = 1, scaleY = 1, progress = 0;
        bool explode = false;
        bool active = false;
    };

    static Effect ComputeEffect(int kind, uint64_t start) {
        Effect e;
        if (start == 0 || kind == kEffectNone) return e;
        uint64_t elapsed = NowMs() - start;
        if (elapsed >= static_cast<uint64_t>(gSettings.effectDurationMs)) return e;
        float p = static_cast<float>(elapsed) / gSettings.effectDurationMs;
        e.active = true;
        e.progress = p;
        const float pi = 3.14159f;
        switch (kind) {
            case kEffectBounce:
                e.bounce = std::sin(p * pi * 3.0f) * (1.0f - p) * 30.0f;
                e.scaleX = e.scaleY = 1.0f + std::sin(p * pi) * 0.4f;
                break;
            case kEffectSpin:
                e.rotation = p * 360.0f;
                e.scaleX = e.scaleY = 1.0f + std::sin(p * pi) * 0.2f;
                break;
            case kEffectIllusion:
                e.scaleX = 1.0f + std::sin(p * pi * 4.0f) * 0.4f * (1.0f - p);
                e.scaleY = 1.0f + std::cos(p * pi * 4.0f) * 0.4f * (1.0f - p);
                e.rotation = std::sin(p * pi * 2.0f) * 15.0f;
                break;
            case kEffectWobble:
                e.rotation = std::sin(p * pi * 8.0f) * 22.0f * (1.0f - p);
                e.scaleX = e.scaleY = 1.0f + std::sin(p * pi * 2.0f) * 0.2f * (1.0f - p);
                break;
            case kEffectExplode:
                e.explode = true;
                break;
        }
        return e;
    }

    // One icon quad with the bounce/spin/illusion/wobble/explode transform --
    // the block the Haiku build repeats for the leaf, apps and trash.
    void DrawIconWithEffect(const GLTexture& tex, const HRect& b, float alpha, const Effect& e) {
        if (tex.id == 0) return;
        const bool top = gSettings.dockLocation == kDockLocationTop;
        float size = b.Width();
        float cx = b.left + size / 2.0f;
        float cy = b.top + size / 2.0f;
        float bounce = top ? -e.bounce : e.bounce;

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glColor4f(alpha, alpha, alpha, alpha);
        glPushMatrix();
        glTranslatef(cx, cy - bounce, 0.0f);
        if (e.rotation != 0.0f) glRotatef(e.rotation, 0, 0, 1);
        glScalef(e.scaleX, e.scaleY, 1.0f);

        if (e.explode && e.progress > 0.0f) {
            // Shatter into a 5x5 grid of flying pieces with a gravity drop,
            // kept inside the dock surface's visible bounds.
            const int cols = 5, rows = 5;
            float sub = size / cols;
            float minY = 8.0f, maxY = fHeight - 8.0f;
            glBegin(GL_QUADS);
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < cols; ++x) {
                    float relX = (x + 0.5f) / cols - 0.5f;
                    float relY = (y + 0.5f) / rows - 0.5f;
                    float scatter = e.progress * 140.0f;
                    float angle = static_cast<float>(x * 37 + y * 59);
                    float dirX = relX + std::cos(angle) * 0.3f;
                    float dirY = relY + std::sin(angle) * 0.3f;
                    float len = std::sqrt(dirX * dirX + dirY * dirY);
                    if (len > 0.001f) { dirX /= len; dirY /= len; }
                    float offX = dirX * scatter * (0.8f + (x % 2) * 0.4f);
                    float offY = dirY * scatter * (0.8f + (y % 2) * 0.4f) + e.progress * e.progress * 50.0f;
                    float pl = -size / 2 + x * sub + offX, pr = pl + sub;
                    float pt = -size / 2 + y * sub + offY, pb = pt + sub;
                    float absTop = (cy - bounce) + pt;
                    float absBottom = (cy - bounce) + pb;
                    if (absTop < minY) { pt += minY - absTop; pb += minY - absTop; }
                    if (absBottom > maxY) { pt -= absBottom - maxY; pb -= absBottom - maxY; }
                    float u1 = static_cast<float>(x) / cols, u2 = static_cast<float>(x + 1) / cols;
                    float v1 = static_cast<float>(y) / rows, v2 = static_cast<float>(y + 1) / rows;
                    glTexCoord2f(u1, v1); glVertex2f(pl, pt);
                    glTexCoord2f(u2, v1); glVertex2f(pr, pt);
                    glTexCoord2f(u2, v2); glVertex2f(pr, pb);
                    glTexCoord2f(u1, v2); glVertex2f(pl, pb);
                }
            }
            glEnd();
        } else {
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(-size / 2, -size / 2);
            glTexCoord2f(1, 0); glVertex2f(size / 2, -size / 2);
            glTexCoord2f(1, 1); glVertex2f(size / 2, size / 2);
            glTexCoord2f(0, 1); glVertex2f(-size / 2, size / 2);
            glEnd();
        }
        glPopMatrix();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    // DrawNativeSystemText(): capsule label centered on centerX, hanging
    // above baselineY (bottom dock) or below it (top dock).
    void DrawCapsule(const std::string& text, float centerX, float baselineY, bool anchorAbove) {
        if (text.empty()) return;
        const GLTexture& t = CapsuleLabel(text);
        float w = static_cast<float>(t.logicalW), h = static_cast<float>(t.logicalH);
        float left = std::clamp(centerX - w / 2.0f, 2.0f, std::max(2.0f, fWidth - w - 2.0f));
        float top = anchorAbove ? baselineY - h : baselineY;
        top = std::clamp(top, 0.0f, std::max(0.0f, fHeight - h));
        DrawTexture(t, left, top, w, h);
    }

    void RenderNow() {
        if (!fConfigured || fEglSurface == EGL_NO_SURFACE) return;
        fDirty = false;
        eglMakeCurrent(gEgl.display, fEglSurface, fEglSurface, gEgl.context);

        UpdateAutoHide();
        UpdateClockTexture();

        glViewport(0, 0, fPixelW, fPixelH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, fWidth, fHeight, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_LINE_SMOOTH);

        // Hit testing / magnification happen in un-slid layout coordinates.
        fLayoutMouseX = fMouseX;
        fLayoutMouseY = fMouseY - DirectionalOffset();
        fHoverActive = fPointerInside && fDockState != STATE_HIDDEN;
        fLayout = ComputeLayout();
        const DockLayout& L = fLayout;
        const bool top = gSettings.dockLocation == kDockLocationTop;
        const float ratio = gSettings.baseIconSize / 48.0f;

        glPushMatrix();
        glTranslatef(0.0f, DirectionalOffset(), 0.0f);

        // ---- Backplate ----------------------------------------------------
        // Haiku reads the panel color from app_server's appearance settings;
        // the default Haiku panel grey is kept here.
        DrawFilledRoundedRect(L.plate, 15.0f, 216 / 255.0f, 216 / 255.0f, 216 / 255.0f, gSettings.dockAlpha);
        DrawOutlineRoundedRect(L.plate, 15.0f, 0.15f, 0.15f, 0.15f, gSettings.dockAlpha);

        auto drawDivider = [&](float x) {
            if (x < 0) return;
            float sx = std::floor(x + 0.5f);
            glLineWidth(2.0f);
            SetColor(0.15f, 0.15f, 0.15f, gSettings.dockAlpha * 0.5f);
            glBegin(GL_LINES);
            glVertex2f(sx, L.plate.top + 8.0f);
            glVertex2f(sx, L.plate.bottom - 8.0f);
            glEnd();
        };
        drawDivider(L.appSeparatorX);
        drawDivider(L.widgetDividerX);

        uint64_t now = NowMs();
        bool anyEffect = false;
        std::string hoveredLabel;
        HRect hoveredLabelIcon;
        const DockApp* labelApp = nullptr;

        for (const DockSlot& s : L.slots) {
            switch (s.kind) {
                case kSlotLeaf: {
                    EnsureLeafIcon();
                    Effect e = (fEffectKey == "leaf") ? ComputeEffect(gSettings.openEffect, fEffectStart) : Effect();
                    anyEffect |= e.active;
                    DrawIconWithEffect(fLeafIcon, s.bounds, 1.0f, e);
                    break;
                }
                case kSlotApp: {
                    DockApp& app = fApps[s.appIndex];
                    app.bounds = s.bounds;
                    EnsureAppIcon(app);
                    Effect e;
                    if (app.closing && app.key == fClosingKey) {
                        e = ComputeEffect(gSettings.closeEffect, fCloseStart);
                    } else if (fEffectKey == "app:" + app.key) {
                        e = ComputeEffect(gSettings.openEffect, fEffectStart);
                    }
                    anyEffect |= e.active;
                    DrawIconWithEffect(app.icon, s.bounds, (app.minimized && !app.closing) ? 0.45f : 1.0f, e);

                    // Running indicator: a small dot on the plate's edge side
                    // (Haiku marks this with dimming alone; with many apps
                    // minimized on Linux the dot helps tell focus apart).
                    if (app.foreground && !app.closing) {
                        float dotY = top ? L.plate.top + 4.0f : L.plate.bottom - 5.0f;
                        float cx = (s.bounds.left + s.bounds.right) / 2.0f;
                        HRect dot{cx - 2.5f, dotY - 1.5f, cx + 2.5f, dotY + 1.5f};
                        DrawFilledRoundedRect(dot, 1.5f, 0.15f, 0.15f, 0.15f, std::max(0.6f, gSettings.dockAlpha));
                    }
                    if (fHoverActive && s.scale > 1.4f && s.bounds.left <= fLayoutMouseX && fLayoutMouseX <= s.bounds.right) {
                        labelApp = &app;
                        hoveredLabelIcon = s.bounds;
                    }
                    break;
                }
                case kSlotTrash: {
                    EnsureTrashIcon();
                    Effect e = (fEffectKey == "trash") ? ComputeEffect(gSettings.openEffect, fEffectStart) : Effect();
                    anyEffect |= e.active;
                    DrawIconWithEffect(fTrashIcon, s.bounds, 1.0f, e);
                    if (fHoverActive && s.bounds.Contains(fLayoutMouseX, fLayoutMouseY)) {
                        hoveredLabel = fTrashFull ? "Trash" : "Trash (empty)";
                        hoveredLabelIcon = s.bounds;
                    }
                    break;
                }
                case kSlotTray:
                    DrawTray(s, &hoveredLabel, &hoveredLabelIcon);
                    break;
                case kSlotClock:
                    DrawClock(s);
                    break;
                case kSlotVolume:
                    DrawVolume(s, ratio);
                    break;
                case kSlotCpu:
                    DrawCpu(s, ratio);
                    break;
                case kSlotWorkspaces:
                    DrawWorkspaces(s, ratio);
                    break;
            }
        }

        // ---- Title overlays --------------------------------------------------
        UpdateHoverTitles(labelApp, hoveredLabelIcon);
        if (gSettings.titleLabel && labelApp && !labelApp->windows.empty() && !gMenus.IsOpen()) {
            // SDL mode: single line above the icon + auto-focus after 750ms.
            float cx = (hoveredLabelIcon.left + hoveredLabelIcon.right) / 2.0f;
            float baseY = top ? hoveredLabelIcon.bottom + 12.0f : hoveredLabelIcon.top - 12.0f;
            std::string title = labelApp->windows.front()->title;
            if (title.empty()) title = labelApp->displayName;
            DrawCapsule(title, cx, baseY, !top);
            UpdateLabelAutoFocus(labelApp, cx, baseY);
        } else if (!labelApp) {
            fLabelHoverStart = 0;
            if (!hoveredLabel.empty() && !gMenus.IsOpen()) {
                float cx = (hoveredLabelIcon.left + hoveredLabelIcon.right) / 2.0f;
                float baseY = top ? hoveredLabelIcon.bottom + 8.0f : hoveredLabelIcon.top - 8.0f;
                DrawCapsule(hoveredLabel, cx, baseY, !top);
            }
        }

        glPopMatrix();

        // Close effect finished -> drop the retained app.
        if (!fClosingKey.empty() && now - fCloseStart >= static_cast<uint64_t>(gSettings.effectDurationMs)) {
            fClosingKey.clear();
            fCloseStart = 0;
            SyncApps();
        }
        if (!anyEffect && fEffectStart && now - fEffectStart >= static_cast<uint64_t>(gSettings.effectDurationMs)) {
            fEffectKey.clear();
            fEffectStart = 0;
        }

        UpdateInputRegion();
        SendIconRectangles();

        fFrameCallback = wl_surface_frame(surface);
        wl_callback_add_listener(fFrameCallback, &kFrameListener, this);
        eglSwapBuffers(gEgl.display, fEglSurface);

        bool animating = anyEffect || !fClosingKey.empty() || std::abs(fCurrentY - fTargetY) > 0.1f
            || (fLabelHoverStart != 0);
        if (animating) fDirty = true;
    }

    void DrawTray(const DockSlot& s, std::string* label, HRect* labelIcon) {
        auto items = gTray.VisibleItems();
        float ratio = gSettings.baseIconSize / 48.0f;
        float icon = TrayIconBase() * s.scale;
        float spacing = 6.0f * ratio * s.scale;
        float x = s.bounds.left;
        for (TrayItem* it : items) {
            EnsureTrayTexture(it, TrayIconBase());
            it->rect = HRect{x, s.bounds.top, x + icon, s.bounds.top + icon};
            float alpha = 1.0f;
            if (it->status == "NeedsAttention") alpha = 0.6f + 0.4f * std::abs(std::sin(NowMs() / 300.0f));
            DrawTexture(it->texture, x, s.bounds.top, icon, icon, alpha);
            if (fHoverActive && it->rect.Contains(fLayoutMouseX, fLayoutMouseY)) {
                *label = it->title;
                *labelIcon = it->rect;
            }
            x += icon + spacing;
        }
    }

    void DrawClock(const DockSlot& s) {
        const bool top = gSettings.dockLocation == kDockLocationTop;
        HRect b{std::floor(s.bounds.left + 0.5f), std::floor(s.bounds.top + 0.5f),
            std::floor(s.bounds.right + 0.5f), std::floor(s.bounds.bottom + 0.5f)};
        if (fHoverActive && b.Contains(fLayoutMouseX, fLayoutMouseY)) {
            time_t raw = time(nullptr);
            struct tm* ti = localtime(&raw);
            int day = ti->tm_mday;
            const char* suffix = "th";
            if (day == 1 || day == 21 || day == 31) suffix = "st";
            else if (day == 2 || day == 22) suffix = "nd";
            else if (day == 3 || day == 23) suffix = "rd";
            char month[16];
            strftime(month, sizeof(month), "%b", ti);
            char date[64];
            snprintf(date, sizeof(date), "%s %d%s %d", month, day, suffix, ti->tm_year + 1900);
            float ratio = gSettings.baseIconSize / 48.0f;
            float cx = (b.left + b.right) / 2.0f;
            if (top) DrawCapsule(date, cx, b.bottom + 16.0f * ratio, false);
            else DrawCapsule(date, cx, b.top - 16.0f * ratio, true);
        }
        DrawTexture(fClockTexture, b.left, b.top, b.Width(), b.Height());
    }

    void DrawVolume(const DockSlot& s, float ratio) {
        fVolumeRect = s.bounds;
        DrawFilledRect(s.bounds, 0.03f, 0.05f, 0.03f, 0.95f);
        HRect fill = s.bounds;
        fill.right = s.bounds.left + s.bounds.Width() * gVolume.level;
        if (gVolume.muted) DrawFilledRect(fill, 0.55f, 0.55f, 0.55f, 0.6f);
        else DrawFilledRect(fill, 0.2f, 1.0f, 0.2f, 0.85f);
        DrawRectOutline(s.bounds, 0.15f, 0.15f, 0.15f, std::max(0.3f, gSettings.dockAlpha * 0.5f));
        if (fHoverActive && s.bounds.Contains(fLayoutMouseX, fLayoutMouseY)) {
            char buf[32];
            snprintf(buf, sizeof(buf), gVolume.muted ? "Muted" : "Volume: %d%%", static_cast<int>(std::lround(gVolume.level * 100)));
            const bool top = gSettings.dockLocation == kDockLocationTop;
            float cx = (s.bounds.left + s.bounds.right) / 2.0f;
            if (top) DrawCapsule(buf, cx, s.bounds.bottom + 14.0f * ratio, false);
            else DrawCapsule(buf, cx, s.bounds.top - 14.0f * ratio, true);
        }
    }

    void DrawCpu(const DockSlot& s, float ratio) {
        const HRect& g = s.bounds;
        DrawFilledRoundedRect(g, 4.0f, 0.03f, 0.03f, 0.05f, 0.95f);
        int numBars = fCpuHistory.empty() ? 16 : std::min<int>(40, fCpuHistory.size());
        float barSpacing = 1.5f * ratio;
        float barWidth = (g.Width() - barSpacing * (numBars + 1)) / numBars;
        bool light = gSettings.dockAlpha < 0.35f;
        glBegin(GL_QUADS);
        for (int i = 0; i < numBars; ++i) {
            float target = (i < static_cast<int>(fCpu.coreLoads.size())) ? fCpu.coreLoads[i] : 0.0f;
            if (i < static_cast<int>(fCpuHistory.size())) {
                fCpuHistory[i] = fCpuHistory[i] * 0.82f + target * 0.18f;
                target = fCpuHistory[i];
            }
            float left = g.left + barSpacing + i * (barWidth + barSpacing);
            float topY = g.bottom - target * (g.Height() - 2.0f) - 1.0f;
            if (light) SetColor(0.68f, 0.25f, 1.00f, 0.95f);
            else SetColor(0.57f, 0.12f, 0.99f, 0.90f);
            glVertex2f(left, topY);
            glVertex2f(left + barWidth, topY);
            glVertex2f(left + barWidth, g.bottom - 1.0f);
            glVertex2f(left, g.bottom - 1.0f);
        }
        glEnd();

        if (fHoverActive && g.Contains(fLayoutMouseX, fLayoutMouseY)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "CPU: %d%%", static_cast<int>(fCpu.Average() * 100.0f));
            if (fCpuTooltipText != buf || fCpuTooltip.id == 0) {
                DeleteTexture(fCpuTooltip);
                Image img = RenderTextImage(buf, 12.0, false, RGBA{0.2, 1.0, 0.2, 1.0}, scale);
                fCpuTooltip = UploadImage(img);
                FreeImage(img);
                fCpuTooltipText = buf;
            }
            float w = fCpuTooltip.logicalW + 12.0f, h = fCpuTooltip.logicalH + 8.0f;
            float left = g.left + g.Width() / 2.0f - w / 2.0f;
            HRect box;
            if (gSettings.dockLocation == kDockLocationTop) box = HRect{left, g.bottom + 1.0f, left + w, g.bottom + 1.0f + h};
            else box = HRect{left, g.top - 1.0f - h, left + w, g.top - 1.0f};
            DrawFilledRect(box, 0.15f, 0.15f, 0.15f, 0.75f);
            DrawRectOutline(box, 0.10f, 0.10f, 0.10f, 0.5f);
            DrawTexture(fCpuTooltip, box.left + 6.0f, box.top + 4.0f, fCpuTooltip.logicalW, fCpuTooltip.logicalH);
        }
    }

    // Shared by drawing and clicking, like GetWorkspaceGridLayout().
    void WorkspaceGrid(int& cols, int& rows, int& count) const {
        count = gWorkspaces.Count();
        cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count)))));
        rows = std::max(1, static_cast<int>(std::ceil(static_cast<float>(count) / cols)));
    }

    HRect WorkspaceTile(const HRect& b, int index, float ratio) const {
        int cols, rows, count;
        WorkspaceGrid(cols, rows, count);
        float gap = 3.0f * ratio;
        float tileW = (b.Width() - gap * (cols + 1)) / cols;
        float tileH = (b.Height() - gap * (rows + 1)) / rows;
        int col = index % cols, row = index / cols;
        float l = b.left + gap + col * (tileW + gap);
        float t = b.top + gap + row * (tileH + gap);
        return HRect{l, t, l + tileW, t + tileH};
    }

    void DrawWorkspaces(const DockSlot& s, float ratio) {
        DrawFilledRoundedRect(s.bounds, 4.0f, 0.03f, 0.03f, 0.05f, 0.95f);
        int cols, rows, count;
        WorkspaceGrid(cols, rows, count);
        int active = gWorkspaces.ActiveIndex();
        for (int ws = 0; ws < count; ++ws) {
            HRect tile = WorkspaceTile(s.bounds, ws, ratio);
            if (ws == active) DrawFilledRect(tile, 0.57f, 0.12f, 0.99f, 0.95f);
            else if (fHoverActive && tile.Contains(fLayoutMouseX, fLayoutMouseY)) DrawFilledRect(tile, 0.45f, 0.45f, 0.50f, 0.90f);
            else DrawFilledRect(tile, 0.22f, 0.22f, 0.26f, 0.90f);
        }
    }

    // =====================================================================
    // HOVER TITLES
    // =====================================================================
    // Haiku mode (TitleListPreviewWindow): a clickable list of the hovered
    // app's windows in a popup above its icon. The popup is its own surface,
    // so opening it never resizes the dock.
    void UpdateHoverTitles(const DockApp* app, const HRect& iconRect) {
        if (!gSettings.titlePopup || gMenus.IsOpen()) {
            if (gMenus.HoverOpen() && !gSettings.titlePopup) gMenus.CloseHover();
            return;
        }
        // A wider proximity zone than the icon itself (Haiku: icon +/- 40px
        // toward the popup) so moving up onto the popup doesn't close it.
        const DockApp* hovered = nullptr;
        HRect hoveredRect;
        const bool top = gSettings.dockLocation == kDockLocationTop;
        if (fHoverActive) {
            for (const DockSlot& s : fLayout.slots) {
                if (s.kind != kSlotApp) continue;
                const HRect& b = s.bounds;
                bool nearY = top ? (fLayoutMouseY >= b.top && fLayoutMouseY <= b.bottom + 40.0f)
                                 : (fLayoutMouseY >= b.top - 40.0f && fLayoutMouseY <= b.bottom);
                if (fLayoutMouseX >= b.left && fLayoutMouseX <= b.right && nearY) {
                    hovered = &fApps[s.appIndex];
                    hoveredRect = b;
                    break;
                }
            }
        }
        (void)app;
        (void)iconRect;
        if (hovered && !hovered->closing && !hovered->windows.empty()) {
            CancelHoverClose();
            if (fHoverKey != hovered->key || !gMenus.HoverOpen()) {
                fHoverKey = hovered->key;
                fHoverRect = hoveredRect;
                fHoverWindowCount = hovered->windows.size();
                PopupAnchor a;
                a.x = static_cast<int>(hoveredRect.left);
                a.w = static_cast<int>(hoveredRect.Width());
                if (top) {
                    a.y = static_cast<int>(hoveredRect.bottom + DirectionalOffset());
                    a.h = 1;
                    a.anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
                    a.gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
                    a.offsetY = 12;
                } else {
                    a.y = static_cast<int>(hoveredRect.top + DirectionalOffset());
                    a.h = 1;
                    a.anchor = XDG_POSITIONER_ANCHOR_TOP;
                    a.gravity = XDG_POSITIONER_GRAVITY_TOP;
                    a.offsetY = -12;
                }
                gMenus.OpenHover(layerSurface, a, WindowListItems(*hovered), true);
            }
        } else if (gMenus.HoverOpen()) {
            ScheduleHoverClose();
        }
    }

    std::vector<MenuItem> WindowListItems(const DockApp& app) {
        std::vector<MenuItem> items;
        for (Toplevel* t : app.windows) {
            MenuItem m;
            m.label = t->title.empty() ? app.displayName : t->title;
            uint64_t uid = t->uid;
            m.action = [this, uid]() {
                for (Toplevel* w : gToplevels.Windows()) {
                    if (w->uid == uid) gToplevels.Activate(w);
                }
                gMenus.CloseHover();
            };
            items.push_back(m);
        }
        return items;
    }

    // The window list changed while its popup is up: refresh in place, or
    // rebuild if the row count changed (an xdg_popup can't just grow).
    void RefreshHoverPopup() {
        if (!gMenus.HoverOpen() || fHoverKey.empty()) return;
        for (auto& app : fApps) {
            if (app.key != fHoverKey || app.closing) continue;
            if (app.windows.size() == fHoverWindowCount) {
                gMenus.UpdateHover(WindowListItems(app));
            } else {
                gMenus.CloseHover();
                fHoverKey.clear();
            }
            return;
        }
        gMenus.CloseHover();
        fHoverKey.clear();
    }

    void ScheduleHoverClose() {
        if (fHoverCloseTimer) return;
        fHoverCloseTimer = RunAfter(250, [this]() {
            fHoverCloseTimer = 0;
            if (gMenus.PointerInHover()) {
                // Still over the popup: keep checking until the pointer leaves it.
                ScheduleHoverClose();
                return;
            }
            bool overIcon = fPointerInside && fHoverRect.Contains(fLayoutMouseX, fLayoutMouseY);
            if (!overIcon) {
                gMenus.CloseHover();
                fHoverKey.clear();
            }
        });
    }

    void CancelHoverClose() {
        if (fHoverCloseTimer) g_source_remove(fHoverCloseTimer);
        fHoverCloseTimer = 0;
    }

    // SDL mode: hovering the label for 750ms focuses every window of the app.
    void UpdateLabelAutoFocus(const DockApp* app, float cx, float baseY) {
        const bool top = gSettings.dockLocation == kDockLocationTop;
        HRect box{cx - 120.0f, top ? baseY - 4.0f : baseY - 14.0f, cx + 120.0f, top ? baseY + 14.0f : baseY + 4.0f};
        if (box.Contains(fLayoutMouseX, fLayoutMouseY)) {
            uint64_t now = NowMs();
            if (fLabelHoverStart == 0 || fLabelHoverKey != app->key) {
                fLabelHoverStart = now;
                fLabelHoverKey = app->key;
            } else if (now - fLabelHoverStart >= 750) {
                ActivateApp(*app);
                fLabelHoverStart = now;
            }
        } else {
            fLabelHoverStart = 0;
        }
    }

    // =====================================================================
    // INPUT REGION / AUTOHIDE / ICON RECTS
    // =====================================================================
    void UpdateInputRegion() {
        HRect r;
        const bool top = gSettings.dockLocation == kDockLocationTop;
        const float sensorHeight = 4.0f;
        if (fDockState == STATE_HIDDEN) {
            // The sensor row along the screen edge that brings the dock back.
            r.left = fLayout.plate.left - 100.0f;
            r.right = fLayout.plate.right + 100.0f;
            r.top = top ? 0.0f : fHeight - sensorHeight;
            r.bottom = top ? sensorHeight : static_cast<float>(fHeight);
        } else {
            float highest = fLayout.plate.top;
            float lowest = fLayout.plate.bottom;
            for (const auto& s : fLayout.slots) {
                highest = std::min(highest, s.bounds.top);
                lowest = std::max(lowest, s.bounds.bottom);
            }
            r.left = fLayout.plate.left - 30.0f;
            r.right = fLayout.plate.right + 30.0f;
            // Extend to the screen edge so the pointer can't slip into the
            // margin between the plate and the edge and lose the dock.
            if (top) {
                r.top = 0.0f;
                r.bottom = lowest + 12.0f + DirectionalOffset();
            } else {
                r.top = highest - 12.0f + DirectionalOffset();
                r.bottom = static_cast<float>(fHeight);
            }
        }
        int x = static_cast<int>(std::floor(r.left)), y = static_cast<int>(std::floor(r.top));
        int w = static_cast<int>(std::ceil(r.right)) - x, h = static_cast<int>(std::ceil(r.bottom)) - y;
        if (x == fRegionX && y == fRegionY && w == fRegionW && h == fRegionH) return;
        fRegionX = x; fRegionY = y; fRegionW = w; fRegionH = h;
        wl_region* region = wl_compositor_create_region(gWl.compositor);
        wl_region_add(region, x, y, std::max(1, w), std::max(1, h));
        wl_surface_set_input_region(surface, region);
        wl_region_destroy(region);
    }

    void UpdateAutoHide() {
        const float sensorHeight = 4.0f;
        uint64_t now = NowMs();
        float dt = fLastAnimTime ? std::min(0.1f, (now - fLastAnimTime) / 1000.0f) : 0.016f;
        fLastAnimTime = now;

        bool keepOpen = fPointerInside || gMenus.IsOpen() || gMenus.HoverOpen() || AppDrawerOpen();
        if (gSettings.autoHide) {
            if (keepOpen) {
                fTargetY = 0.0f;
                if (fDockState == STATE_HIDDEN || fDockState == STATE_HIDING) fDockState = STATE_SHOWING;
            } else if (fHideDelayStart == 0) {
                fHideDelayStart = now;
            }
            if (!keepOpen && now - fHideDelayStart >= 300) {
                fTargetY = static_cast<float>(fHeight) - sensorHeight;
                if (fDockState == STATE_VISIBLE || fDockState == STATE_SHOWING) fDockState = STATE_HIDING;
            }
            if (keepOpen) fHideDelayStart = 0;
            if (!keepOpen && fHideDelayStart != 0 && now - fHideDelayStart < 300) fDirty = true;
        } else {
            fTargetY = 0.0f;
            fDockState = STATE_VISIBLE;
        }

        float smoothing = 1.0f - std::exp(-12.0f * dt);
        if (std::abs(fCurrentY - fTargetY) > 0.1f) {
            fCurrentY += (fTargetY - fCurrentY) * smoothing;
        } else {
            fCurrentY = fTargetY;
            if (fDockState == STATE_SHOWING) fDockState = STATE_VISIBLE;
            if (fDockState == STATE_HIDING) fDockState = STATE_HIDDEN;
        }
    }

    // Minimize animations (KWin's "magic lamp", Hyprland's) target the icon.
    void SendIconRectangles() {
        for (auto& app : fApps) {
            if (app.closing) continue;
            HRect b = app.bounds;
            b.top += DirectionalOffset();
            b.bottom += DirectionalOffset();
            if (std::abs(b.left - app.lastIconRectSent.left) < 1 && std::abs(b.top - app.lastIconRectSent.top) < 1 &&
                std::abs(b.Width() - app.lastIconRectSent.Width()) < 1) continue;
            // Only resend once the zoom has settled; every frame of a hover
            // would otherwise spam the compositor.
            if (fHoverActive) continue;
            app.lastIconRectSent = b;
            for (Toplevel* t : app.windows) {
                gToplevels.SetIconRect(t, surface, static_cast<int>(b.left), static_cast<int>(b.top),
                    static_cast<int>(b.Width()), static_cast<int>(b.Height()));
            }
        }
    }

    // =====================================================================
    // CLICKS (HandleMouseClick)
    // =====================================================================
    PopupAnchor AnchorAbove(const HRect& b) const {
        PopupAnchor a;
        const bool top = gSettings.dockLocation == kDockLocationTop;
        a.x = static_cast<int>(b.left);
        a.w = static_cast<int>(std::max(1.0f, b.Width()));
        if (top) {
            a.y = static_cast<int>(b.bottom + DirectionalOffset());
            a.h = 1;
            a.anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
            a.gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
            a.offsetY = 6;
        } else {
            a.y = static_cast<int>(b.top + DirectionalOffset());
            a.h = 1;
            a.anchor = XDG_POSITIONER_ANCHOR_TOP;
            a.gravity = XDG_POSITIONER_GRAVITY_TOP;
            a.offsetY = -6;
        }
        return a;
    }

    // Sequential toggle shield: a click that just dismissed a menu shouldn't
    // immediately reopen it.
    bool MenuJustClosed() const {
        return NowMs() - gMenus.LastClosedAt() < 150;
    }

    void OpenMenu(const HRect& anchorRect, std::vector<MenuItem> items, uint32_t serial,
        std::function<std::vector<MenuItem>()> live = nullptr, unsigned liveMs = 0) {
        gMenus.Open(layerSurface, AnchorAbove(anchorRect), std::move(items), serial, std::move(live), liveMs);
    }

    void HandleMouseClick(float x, float y, int button, uint32_t serial) {
        fLayoutMouseX = x;
        fLayoutMouseY = y - DirectionalOffset();
        if (fDockState == STATE_HIDDEN) return;
        float lx = fLayoutMouseX, ly = fLayoutMouseY;
        float ratio = gSettings.baseIconSize / 48.0f;

        for (const DockSlot& s : fLayout.slots) {
            // Widgets hit-test against the plate's full height like Haiku's
            // clock/CPU/workspace bounds; icons against themselves.
            HRect hit = s.bounds;
            if (s.kind == kSlotClock || s.kind == kSlotCpu || s.kind == kSlotWorkspaces || s.kind == kSlotVolume) {
                hit.top = std::min(hit.top, fLayout.plate.top);
                hit.bottom = std::max(hit.bottom, fLayout.plate.bottom);
            }
            if (!hit.Contains(lx, ly)) continue;

            switch (s.kind) {
                case kSlotLeaf:
                    if (button == kButtonRight) {
                        if (MenuJustClosed()) return;
                        OpenMenu(s.bounds, LeafMenuItems(), serial);
                    } else if (button == kButtonLeft) {
                        ToggleAppDrawer();
                        if (AppDrawerOpen()) TriggerEffect("leaf");
                    }
                    return;
                case kSlotApp:
                    ClickApp(fApps[s.appIndex], button, s.bounds, serial);
                    return;
                case kSlotTrash:
                    if (button == kButtonLeft) {
                        OpenTrash();
                        TriggerEffect("trash");
                    } else if (button == kButtonMiddle) {
                        EmptyTrash();
                        fLastTrashCheck = 0;
                        TriggerEffect("trash");
                    } else if (button == kButtonRight) {
                        if (MenuJustClosed()) return;
                        std::vector<MenuItem> items;
                        MenuItem empty;
                        empty.label = "Empty Trash";
                        empty.iconName = "trash-empty";
                        empty.enabled = fTrashFull;
                        empty.action = [this]() { EmptyTrash(); fLastTrashCheck = 0; };
                        items.push_back(empty);
                        MenuItem open;
                        open.label = "Open";
                        open.iconName = "document-open";
                        open.action = [this]() { OpenTrash(); TriggerEffect("trash"); };
                        items.push_back(open);
                        OpenMenu(s.bounds, items, serial);
                    }
                    return;
                case kSlotTray:
                    ClickTray(lx, ly, button, serial);
                    return;
                case kSlotClock:
                    if (button == kButtonLeft) LaunchCommand(ClockCommand());
                    return;
                case kSlotVolume:
                    if (button == kButtonMiddle) {
                        gVolume.SetMuted(!gVolume.muted);
                    } else if (button == kButtonLeft) {
                        // A click opens the mixer; a drag sets the level.
                        fVolumePressed = true;
                        fVolumeDragging = false;
                        fVolumePressX = x;
                    }
                    return;
                case kSlotCpu:
                    if ((button == kButtonLeft || button == kButtonRight) && !MenuJustClosed()) {
                        OpenCpuMenu(s.bounds, serial);
                    }
                    return;
                case kSlotWorkspaces:
                    if (button == kButtonLeft) {
                        int cols, rows, count;
                        WorkspaceGrid(cols, rows, count);
                        for (int ws = 0; ws < count; ++ws) {
                            if (WorkspaceTile(s.bounds, ws, ratio).Contains(lx, ly)) {
                                gWorkspaces.Activate(ws);
                                break;
                            }
                        }
                    } else if (button == kButtonRight && !MenuJustClosed()) {
                        std::vector<MenuItem> items;
                        auto list = gWorkspaces.List();
                        for (size_t i = 0; i < list.size(); ++i) {
                            MenuItem m;
                            m.label = list[i].name.empty() ? "Workspace " + std::to_string(i + 1) : list[i].name;
                            m.check = list[i].active ? kRadioOn : kRadioOff;
                            int idx = static_cast<int>(i);
                            m.action = [idx]() { gWorkspaces.Activate(idx); };
                            items.push_back(m);
                        }
                        OpenMenu(s.bounds, items, serial);
                    }
                    return;
            }
        }
    }

    void ActivateApp(const DockApp& app) {
        // Restore every minimized window, then focus the most recently active.
        Toplevel* best = nullptr;
        for (Toplevel* t : app.windows) {
            if (t->minimized) gToplevels.Unminimize(t);
            if (best == nullptr || t->lastActivated > best->lastActivated) best = t;
        }
        gToplevels.Activate(best);
    }

    void ClickApp(DockApp& app, int button, const HRect& b, uint32_t serial) {
        if (app.closing) return;
        if (button == kButtonMiddle) {
            // Close the app (all of its windows).
            if (gSettings.closeEffect != kEffectNone) {
                fEffectKey.clear();
                fEffectStart = 0;
            }
            for (Toplevel* t : app.windows) gToplevels.Close(t);
            return;
        }
        if (button == kButtonRight) {
            if (MenuJustClosed()) return;
            gMenus.CloseHover();
            OpenMenu(b, AppMenuItems(app), serial);
            return;
        }
        if (button != kButtonLeft) return;
        gMenus.CloseHover();
        if (app.foreground && !app.minimized) {
            // Haiku: AS_MINIMIZE_TEAM when the app is already up front.
            for (Toplevel* t : app.windows) gToplevels.Minimize(t);
        } else {
            ActivateApp(app);
            if (gSettings.openEffect != kEffectNone) TriggerEffect("app:" + app.key);
        }
    }

    void ClickTray(float lx, float ly, int button, uint32_t serial) {
        for (TrayItem* it : gTray.VisibleItems()) {
            if (!it->rect.Contains(lx, ly)) continue;
            // Output-local coordinates, the best a Wayland client can offer.
            int ox = static_cast<int>((it->rect.left + it->rect.right) / 2);
            int oy = static_cast<int>(it->rect.top + DirectionalOffset());
            Output* out = enteredOutputs.empty() ? nullptr : *enteredOutputs.begin();
            if (out && gSettings.dockLocation == kDockLocationBottom) oy = out->height - (fHeight - oy);
            std::string service = it->service, path = it->path;
            HRect rect = it->rect;
            auto showMenu = [this, service, path, rect, serial, ox, oy]() {
                TrayItem* item = nullptr;
                for (TrayItem* t : gTray.VisibleItems()) if (t->service == service && t->path == path) item = t;
                if (item == nullptr) return;
                if (!gTray.HasMenu(item)) {
                    gTray.ContextMenu(item, ox, oy);
                    return;
                }
                gTray.FetchMenu(item, [this, rect, serial, item, ox, oy](std::vector<MenuItem> items) {
                    if (items.empty()) {
                        gTray.ContextMenu(item, ox, oy);
                        return;
                    }
                    OpenMenu(rect, std::move(items), serial);
                });
            };
            if (button == kButtonLeft) {
                if (it->itemIsMenu) showMenu();
                else gTray.Activate(it, ox, oy);
            } else if (button == kButtonMiddle) {
                gTray.SecondaryActivate(it, ox, oy);
            } else if (button == kButtonRight) {
                if (MenuJustClosed()) return;
                showMenu();
            }
            return;
        }
    }

    // HandleMouseWheel(): volume over the slider, workspace over the switcher,
    // tray items get Scroll().
    void HandleMouseWheel(int step) {
        float lx = fMouseX, ly = fMouseY - DirectionalOffset();
        for (const DockSlot& s : fLayout.slots) {
            HRect hit = s.bounds;
            hit.top = std::min(hit.top, fLayout.plate.top);
            hit.bottom = std::max(hit.bottom, fLayout.plate.bottom);
            if (!hit.Contains(lx, ly)) continue;
            if (s.kind == kSlotVolume) {
                gVolume.SetLevel(gVolume.level + step * 0.02f);
            } else if (s.kind == kSlotWorkspaces) {
                int count = gWorkspaces.Count();
                int next = std::clamp(gWorkspaces.ActiveIndex() - step, 0, count - 1);
                gWorkspaces.Activate(next);
            } else if (s.kind == kSlotTray) {
                for (TrayItem* it : gTray.VisibleItems()) {
                    if (it->rect.Contains(lx, ly)) gTray.Scroll(it, -step * 120);
                }
            }
            RequestRender();
            return;
        }
    }

    // =====================================================================
    // MENUS
    // =====================================================================
    std::vector<MenuItem> LeafMenuItems() {
        std::vector<MenuItem> items;
        MenuItem prefs;
        prefs.label = "Preferences…";
        prefs.iconName = "preferences-system";
        prefs.action = []() { ShowConfigPanel(); };
        items.push_back(prefs);
        MenuItem places;
        places.label = "Places";
        places.iconName = "folder";
        places.submenu = []() { return BuildPlacesMenu(); };
        items.push_back(places);
        items.push_back(MenuItem::Separator());
        MenuItem about;
        about.label = "About hDesktop";
        about.iconName = "help-about";
        about.action = []() { ShowAboutAlert(); };
        items.push_back(about);
        return items;
    }

    std::vector<MenuItem> AppMenuItems(DockApp& app) {
        std::vector<MenuItem> items;
        if (app.windows.size() > 1 || !app.windows.empty()) {
            items.push_back(MenuItem::Header(app.displayName));
            for (Toplevel* t : app.windows) {
                MenuItem m;
                m.label = t->title.empty() ? app.displayName : t->title;
                m.check = t->activated ? kRadioOn : kCheckNone;
                uint64_t uid = t->uid;
                m.action = [uid]() {
                    for (Toplevel* w : gToplevels.Windows()) if (w->uid == uid) gToplevels.Activate(w);
                };
                items.push_back(m);
            }
            items.push_back(MenuItem::Separator());
        }
        if (app.entry && app.entry->isFileManager) {
            // Tracker's right-click BNavMenu over the file system.
            MenuItem browse;
            browse.label = "Browse";
            browse.iconName = "folder";
            browse.submenu = []() { return BuildPlacesMenu(); };
            items.push_back(browse);
        }
        if (app.entry) {
            MenuItem launch;
            launch.label = "New Window";
            launch.iconName = "window-new";
            std::string id = app.entry->id;
            launch.action = [id]() { if (AppEntry* e = gApps->ById(id)) LaunchApp(e); };
            items.push_back(launch);
        }
        bool anyVisible = false;
        for (Toplevel* t : app.windows) if (!t->minimized) anyVisible = true;
        MenuItem minimize;
        minimize.label = anyVisible ? "Minimize All" : "Restore All";
        minimize.iconName = anyVisible ? "window-minimize" : "window-restore";
        std::string key = app.key;
        minimize.action = [this, key, anyVisible]() {
            for (auto& a : fApps) {
                if (a.key != key) continue;
                if (anyVisible) for (Toplevel* t : a.windows) gToplevels.Minimize(t);
                else ActivateApp(a);
            }
        };
        items.push_back(minimize);
        MenuItem quit;
        quit.label = app.windows.size() > 1 ? "Close All Windows" : "Close";
        quit.iconName = "window-close";
        quit.action = [this, key]() {
            for (auto& a : fApps) if (a.key == key) for (Toplevel* t : a.windows) gToplevels.Close(t);
        };
        items.push_back(quit);
        return items;
    }

    // AsyncCpuMenuRunner::_DisplayCPUGraphMenu()
    void OpenCpuMenu(const HRect& b, uint32_t serial) {
        std::vector<MenuItem> items;

        MenuItem quitApps;
        quitApps.label = "Quit an application";
        quitApps.iconName = "application-exit";
        quitApps.submenu = [this]() { return QuitApplicationItems(); };
        items.push_back(quitApps);

        MenuItem memory;
        memory.label = "Memory usage";
        memory.iconName = "memory";
        memory.submenu = []() { return MemoryItems(); };
        memory.liveMs = 500;
        items.push_back(memory);

        MenuItem threads;
        threads.label = "Processes and CPU usage";
        threads.iconName = "utilities-system-monitor";
        auto sampler = std::make_shared<ProcessSampler>();
        sampler->Sample(); // prime the deltas
        threads.submenu = [this, sampler]() { return CpuProcessItems(*sampler); };
        threads.liveMs = 1000;
        items.push_back(threads);

        std::string profile = CurrentPowerProfile();
        if (!profile.empty()) {
            MenuItem power;
            power.label = "Power saving";
            power.check = (profile == "power-saver") ? kCheckOn : kCheckOff;
            bool saving = (profile == "power-saver");
            power.action = [saving]() {
                RunDetached(std::string("powerprofilesctl set ") + (saving ? "balanced" : "power-saver"));
            };
            items.push_back(power);
        }
        OpenMenu(b, items, serial);
    }

    static bool IsSessionCritical(const std::string& name) {
        static const char* kProtected[] = {
            "systemd", "dbus-daemon", "dbus-broker", "dbus-broker-launch", "pipewire", "pipewire-pulse",
            "wireplumber", "kwin_wayland", "kwin_wayland_wrapper", "Hyprland", "sway", "niri", "labwc",
            "plasmashell", "ksmserver", "kded6", "Xwayland", "xdg-desktop-portal", "gnome-keyring-daemon",
            "kwalletd6", "polkit-kde-authentication-agent-1", "startplasma-wayland", "sddm", "sddm-helper",
            "hdesktop", "hdesktop_linux", "(sd-pam)", "at-spi-bus-launcher", "at-spi2-registryd",
        };
        for (const char* p : kProtected) if (name == p) return true;
        return g_str_has_prefix(name.c_str(), "xdg-desktop-portal") || g_str_has_prefix(name.c_str(), "xdg-");
    }

    std::vector<MenuItem> QuitApplicationItems() {
        ProcessSampler sampler;
        auto procs = sampler.Sample();
        uid_t me = getuid();
        std::map<std::string, std::vector<pid_t>> byName;
        std::map<std::string, std::string> iconFor;
        for (const auto& p : procs) {
            if (p.uid != me || p.exe.empty() || p.pid == getpid()) continue;
            std::string name = AppDatabase::Basename(p.exe);
            if (IsSessionCritical(name) || IsSessionCritical(p.name)) continue;
            byName[name].push_back(p.pid);
            if (!iconFor.count(name)) {
                AppEntry* e = gApps->ResolveExecutable(name);
                iconFor[name] = e ? e->iconName : "application-x-executable";
            }
        }
        std::vector<MenuItem> items;
        for (const auto& kv : byName) {
            MenuItem m;
            m.label = kv.first;
            m.iconName = iconFor[kv.first];
            std::string name = kv.first;
            std::vector<pid_t> pids = kv.second;
            m.action = [name, pids]() {
                ShowAlert("Close Application",
                    "Do you want to close '" + name + "' smoothly?\n\n"
                    "This will send a standard termination request (SIGTERM) to the application.",
                    {"Cancel", "Close App"}, [pids](int choice) {
                        if (choice == 1) for (pid_t p : pids) kill(p, SIGTERM);
                    }, 1);
            };
            items.push_back(m);
            if (items.size() >= 45) break;
        }
        if (items.empty()) {
            MenuItem none;
            none.label = "No applications running";
            none.enabled = false;
            items.push_back(none);
        }
        return items;
    }

    static std::vector<MenuItem> MemoryItems() {
        MemInfo m = MemInfo::Read();
        double total = m.totalKb / 1024.0;
        double used = (m.totalKb - m.availableKb) / 1024.0;
        double pct = total > 0 ? used / total * 100.0 : 0.0;
        auto row = [](const std::string& label, double percent) {
            MenuItem it;
            it.label = label;
            it.barPercent = percent;
            it.barPalette = 1;
            char buf[16];
            snprintf(buf, sizeof(buf), "%3.1f%%", percent);
            it.valueText = buf;
            it.action = []() {};
            it.enabled = true;
            return it;
        };
        char a[64], b[64], c[64];
        snprintf(a, sizeof(a), "Used Physical Memory: %d MB", static_cast<int>(used));
        snprintf(b, sizeof(b), "Free Available RAM: %d MB", static_cast<int>(total - used));
        snprintf(c, sizeof(c), "Total Installed Capacity: %d MB", static_cast<int>(total));
        return {row(a, pct), row(b, 100.0 - pct), row(c, 100.0)};
    }

    std::vector<MenuItem> CpuProcessItems(ProcessSampler& sampler) {
        auto procs = sampler.Sample();
        std::sort(procs.begin(), procs.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
            return a.cpuPercent > b.cpuPercent;
        });
        std::vector<MenuItem> items;
        for (const auto& p : procs) {
            MenuItem m;
            m.label = p.name;
            m.barPercent = p.cpuPercent;
            char buf[16];
            snprintf(buf, sizeof(buf), "%3.1f%%", p.cpuPercent);
            m.valueText = buf;
            AppEntry* e = p.exe.empty() ? nullptr : gApps->ResolveExecutable(AppDatabase::Basename(p.exe));
            m.iconName = e ? e->iconName : "application-x-executable";
            pid_t pid = p.pid;
            std::string name = p.name;
            bool mine = (p.uid == getuid());
            m.enabled = mine;
            m.action = [pid, name]() {
                ShowAlert("Force Terminate",
                    "Are you sure you want to force terminate the process '" + name + "' (PID: " +
                    std::to_string(pid) + ")?\n\nUnsaved progress inside this application will be lost.",
                    {"Cancel", "Force Kill"}, [pid](int choice) {
                        if (choice == 1) kill(pid, SIGKILL);
                    }, 0);
            };
            items.push_back(m);
            if (items.size() >= 30) break;
        }
        return items;
    }

    // =====================================================================
    // STATE
    // =====================================================================
    int fWidth = 0, fHeight = 0;
    int fPixelW = 0, fPixelH = 0;
    bool fConfigured = false;
    bool fDirty = true;
    guint fRenderIdle = 0;
    wl_callback* fFrameCallback = nullptr;
    wl_egl_window* fEglWindow = nullptr;
    EGLSurface fEglSurface = EGL_NO_SURFACE;
    int fRegionX = -1, fRegionY = -1, fRegionW = -1, fRegionH = -1;

    std::vector<DockApp> fApps;
    bool fInitialSyncDone = false;
    DockLayout fLayout;

    float fMouseX = -1000, fMouseY = -1000;
    float fLayoutMouseX = -1000, fLayoutMouseY = -1000;
    bool fPointerInside = false;
    bool fHoverActive = false;

    GLTexture fLeafIcon, fTrashIcon, fClockTexture, fCpuTooltip;
    std::string fLastClockString, fCpuTooltipText;
    std::map<std::string, GLTexture> fLabelCache;
    bool fTrashFull = false;
    uint64_t fLastTrashCheck = 0;

    CpuSampler fCpu;
    std::vector<float> fCpuHistory;

    HRect fVolumeRect;
    bool fVolumePressed = false, fVolumeDragging = false;
    float fVolumePressX = 0;

    std::string fEffectKey;
    uint64_t fEffectStart = 0;
    std::string fClosingKey;
    uint64_t fCloseStart = 0;

    std::string fHoverKey;
    HRect fHoverRect;
    size_t fHoverWindowCount = 0;
    guint fHoverCloseTimer = 0;
    uint64_t fLabelHoverStart = 0;
    std::string fLabelHoverKey;

    AutoHideState fDockState = STATE_VISIBLE;
    float fCurrentY = 0.0f, fTargetY = 0.0f;
    uint64_t fLastAnimTime = 0;
    uint64_t fHideDelayStart = 0;
};

const zwlr_layer_surface_v1_listener DockEngine::kLayerListener = {
    .configure = [](void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
        static_cast<DockEngine*>(data)->Configure(serial, w, h);
    },
    .closed = [](void*, zwlr_layer_surface_v1*) {
        WarnLog("the compositor closed the dock surface\n");
        QuitApplication();
    },
};

const wl_callback_listener DockEngine::kFrameListener = {
    .done = [](void* data, wl_callback* cb, uint32_t) {
        auto* self = static_cast<DockEngine*>(data);
        wl_callback_destroy(cb);
        self->fFrameCallback = nullptr;
        if (self->fDirty) self->RenderNow();
    },
};

static DockEngine* gDock = nullptr;

// =========================================================================
// APP DRAWER (HaikuAppDrawerWindow + DrawerView)
// =========================================================================
// Full-width grid of every installed application (from .desktop files rather
// than /boot/system/apps), favorites first, power buttons in the header. New
// on Linux: type to search, Enter launches the first match.
class AppDrawer : public LayerPanel {
public:
    AppDrawer() {
        Config cfg;
        cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
        cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        const bool dockTop = gSettings.dockLocation == kDockLocationTop;
        // 30px gap on the free edge, 170px on the dock's edge, 40px at the sides.
        cfg.marginLeft = cfg.marginRight = 40;
        cfg.marginTop = dockTop ? 170 : 30;
        cfg.marginBottom = dockTop ? 30 : 170;
        cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
        cfg.nameSpace = "hdesktop-drawer";
        RebuildList();
        CreateLayer(cfg);
    }

    ~AppDrawer() override {
        if (fCloseTimer) g_source_remove(fCloseTimer);
    }

    void RebuildList() {
        fItems.clear();
        std::set<std::string> seen;
        for (const auto& e : gApps->Entries()) {
            if (!e->show || seen.count(e->id)) continue;
            seen.insert(e->id);
            fItems.push_back(Item{e->id, e->name, e->iconName});
        }
        std::sort(fItems.begin(), fItems.end(), [](const Item& a, const Item& b) {
            return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
        });
        Redraw();
    }

    void Paint(cairo_t* cr) override {
        const double w = width, h = height;
        RoundedRectPath(cr, 0, 0, w, h, 10);
        cairo_set_source_rgba(cr, 24 / 255.0, 24 / 255.0, 28 / 255.0, 0.97);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 48 / 255.0, 50 / 255.0, 58 / 255.0, 1);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);

        // ---- Header -----------------------------------------------------
        double tw = 0;
        MeasureText("hdesktop", 20, true, &tw, nullptr);
        DrawText(cr, "hdesktop", (w - tw) / 2, 8, 20, true, RGBA{220 / 255.0, 225 / 255.0, 235 / 255.0, 1});

        DrawHeaderButton(cr, PowerOffRect(), "Power off", RGBA{220 / 255.0, 60 / 255.0, 60 / 255.0, 45 / 255.0},
            RGBA{1, 90 / 255.0, 90 / 255.0, 1}, RGBA{210 / 255.0, 100 / 255.0, 100 / 255.0, 1});
        DrawHeaderButton(cr, RebootRect(), "Restart system", RGBA{60 / 255.0, 140 / 255.0, 220 / 255.0, 45 / 255.0},
            RGBA{90 / 255.0, 175 / 255.0, 1, 1}, RGBA{100 / 255.0, 160 / 255.0, 220 / 255.0, 1});

        // Search field
        HRect sr = SearchRect();
        RoundedRectPath(cr, sr.left, sr.top, sr.Width(), sr.Height(), 4);
        cairo_set_source_rgba(cr, 35 / 255.0, 36 / 255.0, 42 / 255.0, 1);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 70 / 255.0, 110 / 255.0, 200 / 255.0, fSearch.empty() ? 0.35 : 0.9);
        cairo_stroke(cr);
        std::string shown = fSearch.empty() ? "Type to search…" : fSearch;
        RGBA sc = fSearch.empty() ? RGBA{0.5, 0.5, 0.55, 1} : RGBA{0.94, 0.94, 0.96, 1};
        double th = 0;
        MeasureText("Ag", 12, false, nullptr, &th);
        DrawText(cr, shown, sr.left + 10, sr.top + (sr.Height() - th) / 2, 12, false, sc, sr.Width() - 20);
        if (!fSearch.empty()) {
            double cw = 0;
            MeasureText(fSearch, 12, false, &cw, nullptr);
            double cx = std::min(sr.left + 10 + cw + 1, static_cast<double>(sr.right) - 10);
            cairo_set_source_rgba(cr, 0.9, 0.9, 0.95, 0.8);
            cairo_rectangle(cr, cx, sr.top + 7, 1, sr.Height() - 14);
            cairo_fill(cr);
        }

        cairo_set_source_rgba(cr, 50 / 255.0, 52 / 255.0, 60 / 255.0, 1);
        cairo_rectangle(cr, 20, 90, w - 40, 1);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 120 / 255.0, 130 / 255.0, 150 / 255.0, 20 / 255.0);
        cairo_rectangle(cr, 20, 91, w - 40, 1);
        cairo_fill(cr);

        // ---- Grid ---------------------------------------------------------
        cairo_save(cr);
        cairo_rectangle(cr, 0, 95, w, h - 100);
        cairo_clip(cr);
        LayoutGrid();
        for (const auto& sec : fSections) {
            double y = sec.headerY - fScroll;
            if (y > 60 && y < h) {
                DrawText(cr, sec.title, kStartX, y, 12, true, sec.favorites
                    ? RGBA{130 / 255.0, 145 / 255.0, 180 / 255.0, 200 / 255.0}
                    : RGBA{120 / 255.0, 125 / 255.0, 135 / 255.0, 180 / 255.0});
            }
            if (sec.dividerY > 0) {
                cairo_set_source_rgba(cr, 50 / 255.0, 52 / 255.0, 60 / 255.0, 120 / 255.0);
                cairo_rectangle(cr, kStartX, sec.dividerY - fScroll, w - 2 * kStartX, 1);
                cairo_fill(cr);
            }
        }
        for (const auto& cell : fCells) {
            double x = cell.rect.left, y = cell.rect.top - fScroll;
            if (y + kItemH < 95 || y > h) continue;
            const Item& item = fItems[cell.item];
            bool hovered = (&cell - fCells.data()) == fHoveredCell;
            if (hovered) {
                cairo_rectangle(cr, x + 0.5, y + 0.5, kItemW - 1, kItemH - 1);
                cairo_set_source_rgba(cr, 100 / 255.0, 140 / 255.0, 220 / 255.0, 30 / 255.0);
                cairo_fill_preserve(cr);
                cairo_set_source_rgba(cr, 130 / 255.0, 160 / 255.0, 220 / 255.0, 70 / 255.0);
                cairo_stroke(cr);
            }
            IconRef icon = CachedIcon(item.icon, static_cast<int>(std::ceil(48 * scale)));
            if (!icon) {
                icon = MakeIconRef(MakePlaceholderIcon(static_cast<int>(std::ceil(48 * scale)), item.name));
            }
            if (icon) {
                double s = 48.0 / cairo_image_surface_get_width(icon.get());
                cairo_save(cr);
                cairo_translate(cr, x + kItemW / 2 - 24, y + 15);
                cairo_scale(cr, s, s);
                cairo_set_source_surface(cr, icon.get(), 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }
            DrawText(cr, item.name, x + 5, y + 76, 11, false, RGBA{240 / 255.0, 240 / 255.0, 245 / 255.0, 1},
                kItemW - 10, true);
        }
        if (fCells.empty()) {
            std::string msg = fSearch.empty() ? "No applications found" : "No matches for “" + fSearch + "”";
            double mw = 0;
            MeasureText(msg, 13, false, &mw, nullptr);
            DrawText(cr, msg, (w - mw) / 2, 140, 13, false, RGBA{0.6, 0.6, 0.65, 1});
        }
        cairo_restore(cr);

        // Scroll position indicator
        double viewH = h - 100;
        if (fContentHeight > viewH + 1) {
            double barH = std::max(30.0, viewH * viewH / fContentHeight);
            double barY = 95 + (viewH - barH) * (fScroll / (fContentHeight - viewH));
            RoundedRectPath(cr, w - 9, barY, 4, barH, 2);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
            cairo_fill(cr);
        }
    }

    void PointerEnter(double x, double y) override {
        fEntered = true;
        CancelClose();
        PointerMotion(x, y);
    }

    void PointerMotion(double x, double y) override {
        fMouseX = x;
        fMouseY = y;
        int hit = CellAt(x, y);
        bool headerHover = PowerOffRect().Contains(x, y) || RebootRect().Contains(x, y);
        if (hit != fHoveredCell || headerHover != fHeaderHover) {
            fHoveredCell = hit;
            fHeaderHover = headerHover;
            Redraw();
        }
    }

    // Leaving the drawer closes it (unless a context menu from it is open, or
    // the pointer went back to the dock), like the Haiku drawer's 'tick' check.
    void PointerLeave() override {
        fHoveredCell = -1;
        fHeaderHover = false;
        fMouseX = fMouseY = -1;
        Redraw();
        if (fEntered) ScheduleClose();
    }

    void PointerButton(int button, bool pressed, uint32_t serial) override {
        if (!pressed) return;
        double x = gWl.pointerX, y = gWl.pointerY;
        if (button == kButtonLeft && PowerOffRect().Contains(x, y)) {
            RunDetached("systemctl poweroff");
            CloseAppDrawer();
            return;
        }
        if (button == kButtonLeft && RebootRect().Contains(x, y)) {
            RunDetached("systemctl reboot");
            CloseAppDrawer();
            return;
        }
        int cell = CellAt(x, y);
        if (cell < 0) return;
        const Item& item = fItems[fCells[cell].item];
        if (button == kButtonLeft) {
            Launch(item.id);
        } else if (button == kButtonRight) {
            std::string id = item.id;
            bool fav = gSettings.favorites.count(id) > 0;
            MenuItem toggle;
            toggle.label = fav ? "Remove Favorite" : "Add Favorite";
            toggle.iconName = fav ? "starred-symbolic" : "non-starred-symbolic";
            toggle.action = [this, id, fav]() {
                if (fav) gSettings.favorites.erase(id);
                else gSettings.favorites.insert(id);
                SaveConfiguration();
                Redraw();
            };
            MenuItem launch;
            launch.label = "Open";
            launch.action = [this, id]() { Launch(id); };
            PopupAnchor a;
            a.x = static_cast<int>(x);
            a.y = static_cast<int>(y);
            a.anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
            a.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
            a.constraints = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
            gMenus.Open(layerSurface, a, {launch, toggle}, serial);
        }
    }

    void PointerAxis(double dy) override {
        fScroll -= dy * 30.0 * 2;  // B_MOUSE_WHEEL_CHANGED: 30px per notch (x2 to match wheel step feel)
        ClampScroll();
        PointerMotion(fMouseX, fMouseY);
        Redraw();
    }

    void Key(xkb_keysym_t sym, const std::string& utf8) override {
        if (sym == XKB_KEY_Escape || (sym == XKB_KEY_space && fSearch.empty())) {
            CloseAppDrawer();
            return;
        }
        if (sym == XKB_KEY_BackSpace) {
            if (!fSearch.empty()) {
                const char* start = fSearch.c_str();
                const char* prev = g_utf8_find_prev_char(start, start + fSearch.size());
                fSearch.resize(prev ? prev - start : 0);
            }
        } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            LayoutGrid();
            if (!fCells.empty()) Launch(fItems[fCells.front().item].id);
            return;
        } else if (sym == XKB_KEY_Page_Down || sym == XKB_KEY_Down) {
            fScroll += (sym == XKB_KEY_Down) ? kItemH : height - 150;
        } else if (sym == XKB_KEY_Page_Up || sym == XKB_KEY_Up) {
            fScroll -= (sym == XKB_KEY_Up) ? kItemH : height - 150;
        } else if (!utf8.empty() && static_cast<unsigned char>(utf8[0]) >= 0x20 && utf8[0] != 0x7f) {
            fSearch += utf8;
            fScroll = 0;
        } else {
            return;
        }
        ClampScroll();
        Redraw();
    }

private:
    struct Item {
        std::string id, name, icon;
    };
    struct Cell {
        size_t item;
        HRect rect;   // content coordinates (before scrolling)
    };
    struct Section {
        std::string title;
        double headerY;
        double dividerY;
        bool favorites;
    };

    static constexpr double kItemW = 100, kItemH = 110, kStartX = 30, kSpacingX = 24, kSpacingY = 20;

    HRect PowerOffRect() const { return HRect{30, 45, 110, 76}; }
    HRect RebootRect() const { return HRect{120, 45, 225, 76}; }
    HRect SearchRect() const { return HRect{static_cast<float>(width - 330), 45, static_cast<float>(width - 30), 76}; }

    void DrawHeaderButton(cairo_t* cr, const HRect& r, const char* label, RGBA hoverFill, RGBA hoverText, RGBA text) {
        bool hovered = r.Contains(fMouseX, fMouseY);
        cairo_rectangle(cr, r.left + 0.5, r.top + 0.5, r.Width() - 1, r.Height() - 1);
        if (hovered) cairo_set_source_rgba(cr, hoverFill.r, hoverFill.g, hoverFill.b, hoverFill.a);
        else cairo_set_source_rgba(cr, 35 / 255.0, 36 / 255.0, 42 / 255.0, 1);
        cairo_fill_preserve(cr);
        RGBA c = hovered ? hoverText : text;
        cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        double tw = 0, th = 0;
        MeasureText(label, 11, false, &tw, &th);
        DrawText(cr, label, r.left + (r.Width() - tw) / 2, r.top + (r.Height() - th) / 2, 11, false, c);
    }

    bool Matches(const Item& item) const {
        if (fSearch.empty()) return true;
        std::string needle = ToLower(fSearch);
        if (ToLower(item.name).find(needle) != std::string::npos) return true;
        if (ToLower(item.id).find(needle) != std::string::npos) return true;
        AppEntry* e = gApps->ById(item.id);
        if (e && e->info) {
            const char* const* kw = g_desktop_app_info_get_keywords(e->info);
            for (; kw && *kw; ++kw) if (ToLower(*kw).find(needle) != std::string::npos) return true;
            const char* generic = g_desktop_app_info_get_generic_name(e->info);
            if (generic && ToLower(generic).find(needle) != std::string::npos) return true;
        }
        return false;
    }

    void LayoutGrid() {
        fCells.clear();
        fSections.clear();
        int cols = std::max(1, static_cast<int>((width - kStartX * 2) / (kItemW + kSpacingX)));
        double y = 115;
        auto addSection = [&](const std::string& title, bool favorites, const std::vector<size_t>& indices) {
            if (indices.empty()) return;
            Section sec{title, y, -1, favorites};
            y += 25;
            for (size_t i = 0; i < indices.size(); ++i) {
                int c = static_cast<int>(i) % cols, r = static_cast<int>(i) / cols;
                double x = kStartX + c * (kItemW + kSpacingX);
                double cy = y + r * (kItemH + kSpacingY);
                fCells.push_back(Cell{indices[i], HRect{static_cast<float>(x), static_cast<float>(cy),
                    static_cast<float>(x + kItemW), static_cast<float>(cy + kItemH)}});
            }
            int rows = (static_cast<int>(indices.size()) + cols - 1) / cols;
            y += rows * (kItemH + kSpacingY) + 15;
            if (favorites) sec.dividerY = y - 5;
            fSections.push_back(sec);
        };
        std::vector<size_t> favs, rest;
        for (size_t i = 0; i < fItems.size(); ++i) {
            if (!Matches(fItems[i])) continue;
            if (fSearch.empty() && gSettings.favorites.count(fItems[i].id)) favs.push_back(i);
            else rest.push_back(i);
        }
        addSection("Favorites", true, favs);
        addSection(fSearch.empty() ? "Applications" : "Results", false, rest);
        fContentHeight = y + 25 - 95;
    }

    void ClampScroll() {
        LayoutGrid();
        double viewH = height - 100;
        fScroll = std::clamp(fScroll, 0.0, std::max(0.0, fContentHeight - viewH));
    }

    int CellAt(double x, double y) const {
        if (y < 95) return -1;
        for (size_t i = 0; i < fCells.size(); ++i) {
            HRect r = fCells[i].rect;
            r.top -= fScroll;
            r.bottom -= fScroll;
            if (r.Contains(x, y)) return static_cast<int>(i);
        }
        return -1;
    }

    void Launch(const std::string& id) {
        if (AppEntry* e = gApps->ById(id)) LaunchApp(e);
        CloseAppDrawer();
    }

    void ScheduleClose();
    void CancelClose() {
        if (fCloseTimer) g_source_remove(fCloseTimer);
        fCloseTimer = 0;
    }

    std::vector<Item> fItems;
    std::vector<Cell> fCells;
    std::vector<Section> fSections;
    std::string fSearch;
    double fScroll = 0, fContentHeight = 0;
    double fMouseX = -1, fMouseY = -1;
    int fHoveredCell = -1;
    bool fHeaderHover = false;
    bool fEntered = false;
    guint fCloseTimer = 0;
};

static std::unique_ptr<AppDrawer> gDrawer;

static bool AppDrawerOpen() { return gDrawer != nullptr; }

static void CloseAppDrawer() {
    if (!gDrawer) return;
    AppDrawer* raw = gDrawer.release();
    RunLater([raw]() { delete raw; });
    if (gDock) gDock->RequestRender();
}

static void ToggleAppDrawer() {
    if (gDrawer) {
        CloseAppDrawer();
        return;
    }
    gDrawer = std::make_unique<AppDrawer>();
    gDrawer->onClosed = []() { CloseAppDrawer(); };
}

void AppDrawer::ScheduleClose() {
    CancelClose();
    fCloseTimer = g_timeout_add(250, [](gpointer p) -> gboolean {
        auto* self = static_cast<AppDrawer*>(p);
        // Keep it while its own context menu is up or while the pointer is
        // back over the dock (Haiku: "protects the dock area").
        if (gMenus.IsOpen() || gWl.pointerFocus == self || (gDock && gWl.pointerFocus == gDock)) {
            return G_SOURCE_CONTINUE;
        }
        self->fCloseTimer = 0;
        if (gWl.pointerFocus != nullptr && dynamic_cast<PopupMenu*>(gWl.pointerFocus)) return G_SOURCE_REMOVE;
        CloseAppDrawer();
        return G_SOURCE_REMOVE;
    }, this);
}

// =========================================================================
// ALERTS (BAlert)
// =========================================================================
class AlertPanel : public LayerPanel {
public:
    AlertPanel(std::string title, std::string text, std::vector<std::string> buttons,
        std::function<void(int)> onChoice, int defaultButton)
        : fTitle(std::move(title)), fText(std::move(text)), fButtons(std::move(buttons)),
          fOnChoice(std::move(onChoice)), fDefault(defaultButton) {
        if (fButtons.empty()) fButtons.push_back("OK");
        if (fDefault < 0 || fDefault >= static_cast<int>(fButtons.size())) fDefault = fButtons.size() - 1;
        // Measure the wrapped text to size the panel.
        cairo_surface_t* scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cairo_t* cr = cairo_create(scratch);
        PangoLayout* layout = MakeLayout(cr, fText, 12, false);
        pango_layout_set_width(layout, (kWidth - 48) * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(scratch);
        fTextHeight = th;
        Config cfg;
        cfg.width = kWidth;
        cfg.height = static_cast<int>(24 + 24 + 10 + th + 24 + 30 + 20);
        cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
        cfg.nameSpace = "hdesktop-alert";
        CreateLayer(cfg);
    }

    std::function<void()> onDone;

    void Paint(cairo_t* cr) override {
        RoundedRectPath(cr, 0.5, 0.5, width - 1, height - 1, 8);
        cairo_set_source_rgba(cr, 24 / 255.0, 24 / 255.0, 28 / 255.0, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 70 / 255.0, 72 / 255.0, 84 / 255.0, 1);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        DrawText(cr, fTitle, 24, 20, 14, true, RGBA{230 / 255.0, 232 / 255.0, 240 / 255.0, 1});
        PangoLayout* layout = MakeLayout(cr, fText, 12, false);
        pango_layout_set_width(layout, (kWidth - 48) * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        cairo_set_source_rgba(cr, 200 / 255.0, 202 / 255.0, 210 / 255.0, 1);
        cairo_move_to(cr, 24, 52);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        for (size_t i = 0; i < fButtons.size(); ++i) {
            HRect r = ButtonRect(i);
            bool hovered = r.Contains(fMouseX, fMouseY);
            bool isDefault = static_cast<int>(i) == fDefault;
            RoundedRectPath(cr, r.left + 0.5, r.top + 0.5, r.Width() - 1, r.Height() - 1, 4);
            if (isDefault) cairo_set_source_rgba(cr, 70 / 255.0, 110 / 255.0, 200 / 255.0, hovered ? 1.0 : 0.85);
            else cairo_set_source_rgba(cr, hovered ? 55 / 255.0 : 40 / 255.0, hovered ? 57 / 255.0 : 42 / 255.0,
                hovered ? 66 / 255.0 : 50 / 255.0, 1);
            cairo_fill(cr);
            double tw, th;
            MeasureText(fButtons[i], 12, isDefault, &tw, &th);
            DrawText(cr, fButtons[i], r.left + (r.Width() - tw) / 2, r.top + (r.Height() - th) / 2, 12, isDefault,
                RGBA{1, 1, 1, 1});
        }
    }

    void PointerMotion(double x, double y) override { fMouseX = x; fMouseY = y; Redraw(); }
    void PointerEnter(double x, double y) override { PointerMotion(x, y); }
    void PointerLeave() override { fMouseX = fMouseY = -1; Redraw(); }

    void PointerButton(int button, bool pressed, uint32_t) override {
        if (pressed || button != kButtonLeft) return;
        for (size_t i = 0; i < fButtons.size(); ++i) {
            if (ButtonRect(i).Contains(gWl.pointerX, gWl.pointerY)) {
                Finish(static_cast<int>(i));
                return;
            }
        }
    }

    void Key(xkb_keysym_t sym, const std::string&) override {
        if (sym == XKB_KEY_Escape) Finish(0);
        else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) Finish(fDefault);
    }

    void Finish(int choice) {
        if (fFinished) return;
        fFinished = true;
        auto cb = fOnChoice;
        auto done = onDone;
        RunLater([cb, choice]() { if (cb) cb(choice); });
        if (done) done();
    }

private:
    static constexpr int kWidth = 440;

    HRect ButtonRect(size_t i) const {
        float bw = 110, bh = 30, gap = 10;
        float right = width - 24.0f;
        size_t fromRight = fButtons.size() - 1 - i;
        float r = right - fromRight * (bw + gap);
        float bottom = height - 20.0f;
        return HRect{r - bw, bottom - bh, r, bottom};
    }

    std::string fTitle, fText;
    std::vector<std::string> fButtons;
    std::function<void(int)> fOnChoice;
    int fDefault = 0;
    int fTextHeight = 0;
    double fMouseX = -1, fMouseY = -1;
    bool fFinished = false;
};

static std::vector<std::unique_ptr<AlertPanel>> gAlerts;

static void ShowAlert(const std::string& title, const std::string& text, std::vector<std::string> buttons,
    std::function<void(int)> onChoice, int defaultButton) {
    auto alert = std::make_unique<AlertPanel>(title, text, std::move(buttons), std::move(onChoice), defaultButton);
    AlertPanel* raw = alert.get();
    auto remove = [raw]() {
        for (auto it = gAlerts.begin(); it != gAlerts.end(); ++it) {
            if (it->get() == raw) {
                AlertPanel* p = it->release();
                gAlerts.erase(it);
                RunLater([p]() { delete p; });
                return;
            }
        }
    };
    raw->onDone = remove;
    raw->onClosed = remove;
    gAlerts.push_back(std::move(alert));
}

static void ShowAboutAlert() {
    ShowAlert("About hdesktop",
        "hdesktop OpenGL Dock\nMIT License\nVersion " APP_LOCAL_VERSION " (Wayland)\n(c) 2026 ablyss\n\nEnjoy!",
        {"Awesome!"}, nullptr, 0);
}

// =========================================================================
// SETTINGS PANEL (ConfigView / HaikuConfigWindow)
// =========================================================================
class ConfigPanel : public LayerPanel {
public:
    ConfigPanel() {
        BuildWidgets();
        Config cfg;
        cfg.width = kWidth;
        cfg.height = static_cast<int>(fContentHeight);
        cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
        cfg.nameSpace = "hdesktop-settings";
        CreateLayer(cfg);
    }

    void Paint(cairo_t* cr) override {
        const RGBA text{220 / 255.0, 225 / 255.0, 235 / 255.0, 1};
        const RGBA dim{150 / 255.0, 155 / 255.0, 168 / 255.0, 1};
        RoundedRectPath(cr, 0.5, 0.5, width - 1, height - 1, 10);
        cairo_set_source_rgba(cr, 24 / 255.0, 24 / 255.0, 28 / 255.0, 0.98);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 60 / 255.0, 62 / 255.0, 72 / 255.0, 1);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);

        double tw;
        MeasureText("hdesktop settings", 16, true, &tw, nullptr);
        DrawText(cr, "hdesktop settings", (width - tw) / 2, 12, 16, true, text);

        for (size_t i = 0; i < fWidgets.size(); ++i) {
            Widget& w = fWidgets[i];
            bool hovered = static_cast<int>(i) == fHovered;
            switch (w.type) {
                case Widget::kButton: {
                    RoundedRectPath(cr, w.rect.left + 0.5, w.rect.top + 0.5, w.rect.Width() - 1, w.rect.Height() - 1, 4);
                    RGBA base = w.accent;
                    cairo_set_source_rgba(cr, base.r, base.g, base.b, hovered ? 0.35 : 0.18);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgba(cr, base.r, base.g, base.b, hovered ? 1.0 : 0.7);
                    cairo_stroke(cr);
                    double bw, bh;
                    MeasureText(w.label, 12, false, &bw, &bh);
                    DrawText(cr, w.label, w.rect.left + (w.rect.Width() - bw) / 2, w.rect.top + (w.rect.Height() - bh) / 2,
                        12, false, text);
                    break;
                }
                case Widget::kCheck: {
                    bool on = w.getBool();
                    HRect box{w.rect.left, w.rect.top + 3, w.rect.left + 14, w.rect.top + 17};
                    RoundedRectPath(cr, box.left + 0.5, box.top + 0.5, 13, 13, 2);
                    cairo_set_source_rgba(cr, on ? 70 / 255.0 : 35 / 255.0, on ? 110 / 255.0 : 36 / 255.0,
                        on ? 200 / 255.0 : 42 / 255.0, 1);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgba(cr, 110 / 255.0, 115 / 255.0, 130 / 255.0, hovered ? 1 : 0.7);
                    cairo_stroke(cr);
                    if (on) {
                        cairo_set_source_rgb(cr, 1, 1, 1);
                        cairo_set_line_width(cr, 1.8);
                        cairo_move_to(cr, box.left + 3, box.top + 7);
                        cairo_line_to(cr, box.left + 6, box.top + 10.5);
                        cairo_line_to(cr, box.left + 11, box.top + 3.5);
                        cairo_stroke(cr);
                        cairo_set_line_width(cr, 1);
                    }
                    DrawText(cr, w.label, w.rect.left + 22, w.rect.top + 1, 12, false, w.enabled ? text : dim);
                    break;
                }
                case Widget::kLabel:
                    DrawText(cr, w.label, w.rect.left, w.rect.top, w.small ? 10 : 12, !w.small, w.small ? dim : text,
                        w.rect.Width());
                    break;
                case Widget::kSegmented: {
                    int count = static_cast<int>(w.options.size());
                    float segW = w.rect.Width() / count;
                    int current = w.getInt();
                    for (int s = 0; s < count; ++s) {
                        HRect r{w.rect.left + s * segW, w.rect.top, w.rect.left + (s + 1) * segW, w.rect.bottom};
                        bool sel = (s == current);
                        bool hov = hovered && r.Contains(fMouseX, fMouseY);
                        cairo_rectangle(cr, r.left + 0.5, r.top + 0.5, r.Width() - 1, r.Height() - 1);
                        if (sel) cairo_set_source_rgba(cr, 70 / 255.0, 110 / 255.0, 200 / 255.0, 1);
                        else cairo_set_source_rgba(cr, hov ? 50 / 255.0 : 35 / 255.0, hov ? 52 / 255.0 : 36 / 255.0,
                            hov ? 62 / 255.0 : 42 / 255.0, 1);
                        cairo_fill_preserve(cr);
                        cairo_set_source_rgba(cr, 60 / 255.0, 62 / 255.0, 72 / 255.0, 1);
                        cairo_stroke(cr);
                        double sw, sh;
                        MeasureText(w.options[s], 11, sel, &sw, &sh);
                        DrawText(cr, w.options[s], r.left + (r.Width() - sw) / 2, r.top + (r.Height() - sh) / 2, 11, sel, text);
                    }
                    break;
                }
                case Widget::kSlider: {
                    double v = w.getDouble();
                    char valueBuf[48];
                    snprintf(valueBuf, sizeof(valueBuf), "%s: %d", w.label.c_str(), static_cast<int>(std::lround(v)));
                    DrawText(cr, valueBuf, w.rect.left, w.rect.top, 12, false, text);
                    double trackY = w.rect.top + 26;
                    double frac = (v - w.min) / (w.max - w.min);
                    RoundedRectPath(cr, w.rect.left, trackY - 3, w.rect.Width(), 6, 3);
                    cairo_set_source_rgba(cr, 45 / 255.0, 46 / 255.0, 54 / 255.0, 1);
                    cairo_fill(cr);
                    RoundedRectPath(cr, w.rect.left, trackY - 3, w.rect.Width() * frac, 6, 3);
                    cairo_set_source_rgba(cr, 70 / 255.0, 110 / 255.0, 200 / 255.0, 1);
                    cairo_fill(cr);
                    cairo_arc(cr, w.rect.left + w.rect.Width() * frac, trackY, hovered || fDragging == static_cast<int>(i) ? 8 : 7, 0, 2 * M_PI);
                    cairo_set_source_rgb(cr, 0.92, 0.93, 0.96);
                    cairo_fill(cr);
                    double lw;
                    DrawText(cr, w.minLabel, w.rect.left, trackY + 10, 10, false, dim);
                    MeasureText(w.maxLabel, 10, false, &lw, nullptr);
                    DrawText(cr, w.maxLabel, w.rect.right - lw, trackY + 10, 10, false, dim);
                    break;
                }
            }
        }
    }

    void PointerEnter(double x, double y) override { PointerMotion(x, y); }
    void PointerLeave() override { fHovered = -1; fMouseX = fMouseY = -1; Redraw(); }

    void PointerMotion(double x, double y) override {
        fMouseX = x;
        fMouseY = y;
        if (fDragging >= 0) {
            SetSliderFromX(fWidgets[fDragging], x);
            Redraw();
            return;
        }
        int hit = WidgetAt(x, y);
        if (hit != fHovered || (hit >= 0 && fWidgets[hit].type == Widget::kSegmented)) {
            fHovered = hit;
            Redraw();
        }
    }

    void PointerButton(int button, bool pressed, uint32_t) override {
        if (button != kButtonLeft) return;
        if (!pressed) {
            if (fDragging >= 0) {
                fDragging = -1;
                SaveConfiguration();
            }
            return;
        }
        int hit = WidgetAt(fMouseX, fMouseY);
        if (hit < 0) return;
        Widget& w = fWidgets[hit];
        switch (w.type) {
            case Widget::kButton:
                if (w.onClick) RunLater(w.onClick);
                break;
            case Widget::kCheck:
                if (w.enabled) {
                    w.setBool(!w.getBool());
                    Apply();
                }
                break;
            case Widget::kSegmented: {
                int count = static_cast<int>(w.options.size());
                int s = std::clamp(static_cast<int>((fMouseX - w.rect.left) / (w.rect.Width() / count)), 0, count - 1);
                w.setInt(s);
                Apply();
                break;
            }
            case Widget::kSlider:
                fDragging = hit;
                SetSliderFromX(w, fMouseX);
                break;
            default:
                break;
        }
        Redraw();
    }

    void Key(xkb_keysym_t sym, const std::string&) override {
        if (sym == XKB_KEY_Escape) Close();
    }

    std::function<void()> onDone;

private:
    struct Widget {
        enum Type { kButton, kCheck, kLabel, kSegmented, kSlider } type;
        std::string label;
        HRect rect;
        bool enabled = true;
        bool small = false;
        RGBA accent{0.4, 0.45, 0.6, 1};
        std::function<void()> onClick;
        std::function<bool()> getBool;
        std::function<void(bool)> setBool;
        std::vector<std::string> options;
        std::function<int()> getInt;
        std::function<void(int)> setInt;
        double min = 0, max = 1;
        std::string minLabel, maxLabel;
        std::function<double()> getDouble;
        std::function<void(double)> setDouble;
    };

    static constexpr int kWidth = 560;

    void BuildWidgets() {
        float y = 44;
        auto button = [&](const std::string& label, RGBA accent, std::function<void()> fn) {
            Widget w;
            w.type = Widget::kButton;
            w.label = label;
            w.accent = accent;
            w.onClick = std::move(fn);
            w.rect = HRect{kWidth / 2.0f - 100, y, kWidth / 2.0f + 100, y + 26};
            fWidgets.push_back(w);
            y += 34;
        };
        button("Shutdown hDesktop", RGBA{0.86, 0.3, 0.3, 1}, []() { QuitApplication(); });
        button("About hdesktop", RGBA{0.35, 0.55, 0.86, 1}, []() { ShowAboutAlert(); });
        y += 10;

        auto check = [&](const std::string& label, bool* value, float x = 40, bool newRow = true, bool enabled = true) {
            Widget w;
            w.type = Widget::kCheck;
            w.label = label;
            w.enabled = enabled;
            w.getBool = [value]() { return *value; };
            w.setBool = [value](bool v) { *value = v; };
            double tw;
            MeasureText(label, 12, false, &tw, nullptr);
            w.rect = HRect{x, y, static_cast<float>(x + 26 + tw), y + 20};
            fWidgets.push_back(w);
            if (newRow) y += 24;
        };
        check("Enable Auto-Hide", &gSettings.autoHide);
        check("Enable System Tray", &gSettings.showSystemTray);
        check("Keep Dock Above Windows", &gSettings.keepAboveWindows);
        check("Reserve Screen Space (windows stop at the dock)", &gSettings.reserveSpace);
        check("Title Overlays: Popup List", &gSettings.titlePopup, 40, false);
        size_t popupIdx = fWidgets.size() - 1;
        check("Label Mode", &gSettings.titleLabel, 317);
        size_t labelIdx = fWidgets.size() - 1;
        // The two title overlay modes are mutually exclusive.
        fWidgets[popupIdx].setBool = [](bool v) { gSettings.titlePopup = v; if (v) gSettings.titleLabel = false; };
        fWidgets[labelIdx].setBool = [](bool v) { gSettings.titleLabel = v; if (v) gSettings.titlePopup = false; };
        check("Enable Workspace Switcher", &gSettings.workspaceSwitcher, 40, true, gWorkspaces.Available());
        check("24-Hour Clock", &gSettings.clock24h);
        check("Check for Updates", &gSettings.checkForUpdates);

        {
            Widget info;
            info.type = Widget::kLabel;
            info.small = true;
            info.label = std::string("Taskbar: ") + gToplevels.BackendName() + "   ·   Workspaces: " +
                gWorkspaces.BackendName();
            info.rect = HRect{40, y + 2, kWidth - 40.0f, y + 16};
            fWidgets.push_back(info);
            y += 28;
        }

        auto segmented = [&](const std::string& label, std::vector<std::string> options, int* value) {
            Widget l;
            l.type = Widget::kLabel;
            l.label = label;
            l.rect = HRect{40, y, kWidth - 40.0f, y + 16};
            fWidgets.push_back(l);
            y += 22;
            Widget s;
            s.type = Widget::kSegmented;
            s.options = std::move(options);
            s.getInt = [value]() { return *value; };
            s.setInt = [value](int v) { *value = v; };
            s.rect = HRect{40, y, kWidth - 40.0f, y + 26};
            fWidgets.push_back(s);
            y += 38;
        };
        segmented("Dock Location:", {"Bottom", "Top"}, &gSettings.dockLocation);
        std::vector<std::string> effects(kEffectNames, kEffectNames + kEffectCount);
        effects[0] = "No Effects";
        segmented("Open App Effects:", effects, &gSettings.openEffect);
        segmented("Close App Effects:", effects, &gSettings.closeEffect);

        auto slider = [&](const std::string& label, double min, double max, const char* minL, const char* maxL,
            std::function<double()> get, std::function<void(double)> set) {
            Widget w;
            w.type = Widget::kSlider;
            w.label = label;
            w.min = min;
            w.max = max;
            w.minLabel = minL;
            w.maxLabel = maxL;
            w.getDouble = std::move(get);
            w.setDouble = std::move(set);
            w.rect = HRect{40, y, kWidth - 40.0f, y + 52};
            fWidgets.push_back(w);
            y += 58;
        };
        slider("Effect Speed (ms)", 200, 1500, "Fast", "Slow",
            []() { return static_cast<double>(gSettings.effectDurationMs); },
            [](double v) { gSettings.effectDurationMs = static_cast<int>(std::lround(v)); });
        slider("Dock Transparency", 0, 100, "Transparent", "Opaque",
            []() { return gSettings.dockAlpha * 100.0; },
            [](double v) { gSettings.dockAlpha = static_cast<float>(v / 100.0); });
        slider("Icon Size", 32, 72, "Small", "Large",
            []() { return static_cast<double>(gSettings.baseIconSize); },
            [](double v) { gSettings.baseIconSize = static_cast<float>(std::lround(v)); });

        y += 6;
        button("Close", RGBA{0.45, 0.47, 0.55, 1}, [this]() { Close(); });
        fContentHeight = y + 8;
    }

    int WidgetAt(double x, double y) const {
        for (size_t i = 0; i < fWidgets.size(); ++i) {
            HRect r = fWidgets[i].rect;
            if (fWidgets[i].type == Widget::kSlider) { r.top += 14; r.bottom = r.top + 26; r.left -= 8; r.right += 8; }
            if (fWidgets[i].type != Widget::kLabel && r.Contains(x, y)) return static_cast<int>(i);
        }
        return -1;
    }

    void SetSliderFromX(Widget& w, double x) {
        double frac = std::clamp((x - w.rect.left) / w.rect.Width(), 0.0, 1.0);
        double v = w.min + frac * (w.max - w.min);
        if (std::lround(v) == std::lround(w.getDouble())) return;
        w.setDouble(v);
        if (gDock) gDock->SettingsChanged();
    }

    void Apply() {
        SaveConfiguration();
        if (gDock) gDock->SettingsChanged();
    }

    void Close() {
        if (onDone) onDone();
    }

    std::vector<Widget> fWidgets;
    float fContentHeight = 700;
    int fHovered = -1;
    int fDragging = -1;
    double fMouseX = -1, fMouseY = -1;
};

static std::unique_ptr<ConfigPanel> gConfig;

static void ShowConfigPanel() {
    if (gConfig) return;
    gConfig = std::make_unique<ConfigPanel>();
    auto close = []() {
        if (!gConfig) return;
        ConfigPanel* raw = gConfig.release();
        RunLater([raw]() { delete raw; });
    };
    gConfig->onDone = close;
    gConfig->onClosed = close;
}

// =========================================================================
// UPDATE CHECKER (libcurl, as in the Haiku build)
// =========================================================================
static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static int FlattenVersion(const char* s) {
    int a = 0, b = 0, c = 0;
    const char* v = strchr(s, 'v');
    if (v == nullptr || sscanf(v, "v%d.%d.%d", &a, &b, &c) != 3) {
        const char* d = s;
        while (*d && !isdigit(static_cast<unsigned char>(*d))) ++d;
        sscanf(d, "%d.%d.%d", &a, &b, &c);
    }
    return a * 10000 + b * 100 + c;
}

static void SendNotification(const std::string& title, const std::string& body) {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus == nullptr) return;
    GVariantBuilder actions, hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify",
        g_variant_new("(susssasa{sv}i)", "hDesktop", 0u, "system-software-update", title.c_str(), body.c_str(),
            &actions, &hints, -1),
        nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr, nullptr);
    g_object_unref(bus);
}

static void StartUpdateChecker() {
    if (!gSettings.checkForUpdates) return;
    std::thread([]() {
        // Let the dock finish its first frames first.
        std::this_thread::sleep_for(std::chrono::seconds(5));
        CURL* curl = curl_easy_init();
        if (curl == nullptr) return;
        std::string buffer;
        curl_easy_setopt(curl, CURLOPT_URL, "https://raw.githubusercontent.com/ablyssx74/hdesktop/refs/heads/main/VERSION");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "hdesktop/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        CURLcode res = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK || status != 200) {
            DebugLog("update check failed: %s (HTTP %ld)\n", curl_easy_strerror(res), status);
            return;
        }
        std::string remote = Trim(buffer);
        DebugLog("update check: local %s, remote %s\n", APP_LOCAL_VERSION, remote.c_str());
        if (!remote.empty() && FlattenVersion(remote.c_str()) > FlattenVersion(APP_LOCAL_VERSION)) {
            auto* text = new std::string("A newer version of hDesktop is available! (" + remote + ")");
            g_idle_add([](gpointer p) -> gboolean {
                auto* t = static_cast<std::string*>(p);
                SendNotification("Update Available", *t);
                delete t;
                return G_SOURCE_REMOVE;
            }, text);
        }
    }).detach();
}

// =========================================================================
// MAIN
// =========================================================================
static void PrintUsage(const char* argv0) {
    printf("Usage: %s [options]\n"
           "  -d, --debug          print diagnostic output\n"
           "  -o, --output NAME    put the dock on this output (e.g. DP-1)\n"
           "  -v, --version        print the version and exit\n"
           "  -h, --help           show this help\n", argv0);
}

// A second dock on top of the first would be confusing; hold a bus name.
static bool AcquireSingleInstance() {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus == nullptr) return true; // no bus: nothing to coordinate with
    GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", "net.epluribusunix.hDesktop", 4u /* DO_NOT_QUEUE */),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
    if (reply == nullptr) return true;
    guint32 result = 0;
    g_variant_get(reply, "(u)", &result);
    g_variant_unref(reply);
    return result == 1 || result == 4; // primary owner / already owner
}

int main(int argc, char* argv[]) {
    std::string outputOverride;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            gDebugEnabled = true;
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            outputOverride = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("hdesktop %s (Wayland)\n", APP_LOCAL_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        } else {
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // libcurl's global init isn't thread-safe; do it before the checker thread.
    curl_global_init(CURL_GLOBAL_DEFAULT);
    signal(SIGPIPE, SIG_IGN);

    LoadConfiguration();
    if (!outputOverride.empty()) gSettings.output = outputOverride;

    if (!AcquireSingleInstance()) {
        WarnLog("hDesktop is already running.\n");
        return 0;
    }

    gWl.display = wl_display_connect(nullptr);
    if (gWl.display == nullptr) {
        WarnLog("cannot connect to a Wayland compositor (is WAYLAND_DISPLAY set?)\n");
        return 1;
    }
    gWl.registry = wl_display_get_registry(gWl.display);
    wl_registry_add_listener(gWl.registry, &kRegistryListener, nullptr);
    wl_display_roundtrip(gWl.display);
    // Attach the window/workspace listeners before anything else dispatches:
    // the compositor announces existing windows right after the bind, and
    // events for a proxy without a listener are silently dropped.
    gToplevels.Init();
    gWorkspaces.Init();
    wl_display_roundtrip(gWl.display); // output names/scales, seat capabilities, initial windows

    if (gWl.compositor == nullptr || gWl.shm == nullptr) {
        WarnLog("the compositor is missing core Wayland interfaces\n");
        return 1;
    }
    if (gWl.layerShell == nullptr) {
        WarnLog("this compositor doesn't support wlr-layer-shell, which hDesktop needs to place the dock.\n"
                "Supported: KDE Plasma (KWin), Hyprland, Sway, niri, labwc, Wayfire, COSMIC, river.\n"
                "GNOME/Mutter is not supported.\n");
        return 1;
    }
    if (!gSettings.output.empty() && FindOutput(gSettings.output) == nullptr) {
        WarnLog("output '%s' not found, letting the compositor choose\n", gSettings.output.c_str());
    }
    if (!InitEgl()) {
        WarnLog("could not set up OpenGL through EGL\n");
        return 1;
    }

    gMainLoop = g_main_loop_new(nullptr, FALSE);
    gIconTheme = new IconTheme();
    gApps = new AppDatabase();

    // Installed/removed applications: refresh the database and every
    // pointer into it.
    GAppInfoMonitor* monitor = g_app_info_monitor_get();
    g_signal_connect(monitor, "changed", G_CALLBACK(+[](GAppInfoMonitor*, gpointer) {
        RunLater([]() {
            gApps->Reload();
            if (gDock) gDock->AppsReloaded();
            if (gDrawer) gDrawer->RebuildList();
        });
    }), nullptr);

    gVolume.Init();
    gTray.Init();
    if (!gToplevels.Available()) {
        WarnLog("no window-management protocol available: the taskbar part of the dock is disabled.\n");
        if (gWl.plasmaWindows == nullptr && DesktopIs("KDE")) {
            WarnLog("On KDE Plasma, install hDesktop (make -f Makefile.linux install) so KWin grants it "
                    "org_kde_plasma_window_management.\n");
        }
    }

    gDock = new DockEngine();
    gDock->Create();
    gOnOutputsChanged = []() { if (gDock) gDock->RequestRender(); };

    AttachWaylandSource();
    g_unix_signal_add(SIGINT, [](gpointer) -> gboolean { QuitApplication(); return G_SOURCE_REMOVE; }, nullptr);
    g_unix_signal_add(SIGTERM, [](gpointer) -> gboolean { QuitApplication(); return G_SOURCE_REMOVE; }, nullptr);

    StartUpdateChecker();
    g_main_loop_run(gMainLoop);

    gMenus.CloseAll();
    gMenus.CloseHover();
    gDrawer.reset();
    gConfig.reset();
    gAlerts.clear();
    wl_display_flush(gWl.display);
    curl_global_cleanup();
    return 0;
}
