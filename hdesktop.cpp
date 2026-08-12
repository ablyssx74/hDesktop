/*
 * Copyright 2026, Kris Beazley hDesktop@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */ 
 
#include <Alert.h>
#include <algorithm>
#include <AppKit.h>
#include <AppServerLink.h> 
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <cmath>
#include <cstdio>
#include <cstdlib> 
#include <cstring>
#include <ctime>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <fs_attr.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <InterfaceKit.h>
#include <iostream>
#include <map>
#include <MediaNode.h>
#include <MediaRoster.h>
#include <MessageRunner.h> 
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <ParameterWeb.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Rect.h> 
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>
#include <set>
#include <Shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <StorageKit.h>
#include <String.h>
#include <string>
#include <SupportDefs.h>
#include <SupportKit.h> 
#include <TranslationUtils.h>
#include <vector>
#include <View.h>
#include <Window.h>
#include <NavMenu.h> 
#include <WindowInfo.h>



class HaikuGlDesktopEngine;
class HaikuAppDrawerWindow; 
HaikuAppDrawerWindow* gActiveDrawerInstance = nullptr; 
BWindow* gActiveConfigInstance = nullptr; 
std::set<std::string> gFavoritePaths; 
bool autoHideEnabled; 
bool showSystemTray; 
bool dockAlwaysOnTop;
bool fShowTitleOverlays;
void SaveConfiguration(); 
float fBaseIconSize = 48.0f;
float maxDockHeight = 160.0f;
float fDockAlpha = 0.40f; 
const char* const kSettingsIconSizeKey = "base_icon_size";
const char* const kSettingsAlphaKey = "dock_alpha";
 


rgb_color GetLiveSystemBackgroundColor() {
    // Default fallback color (Standard Haiku Grey)
    rgb_color color = { 216, 216, 216, 255 }; 
    
    BFile file("/boot/home/config/settings/system/app_server/appearance", B_READ_ONLY);
    if (file.InitCheck() != B_OK) return color;

    BMessage settingsMsg;
    if (settingsMsg.Unflatten(&file) != B_OK) return color;

    int32 packedColorValue = 0;
    // Keep targeting "color2" since you confirmed color1/2/3 shift with your panel preferences!
    if (settingsMsg.FindInt32("color2", &packedColorValue) == B_OK) {
        
        // CORRECTED BYTE OFFSET SHIFTS FOR LITTLE-ENDIAN HAIKU MESSAGES:
        color.red   = (uint8)(packedColorValue & 0xFF);
        color.green = (uint8)((packedColorValue >> 8) & 0xFF);
        color.blue  = (uint8)((packedColorValue >> 16) & 0xFF);
        color.alpha = (uint8)((packedColorValue >> 24) & 0xFF);

        // Safety fallback: if Alpha channel decodes to 0, force full opacity
        if (color.alpha == 0) color.alpha = 255;
    }
    
    return color;
}


enum {
	SDL_EVENT_WALLPAPER_CHANGED = SDL_USEREVENT + 1,
    MSG_AUTOHIDE_TOGGLED   = 'ahtg',
    MSG_SYSTEMTRAY_TOGGLED = 'sttg',
    MSG_TEXTOVERLAYS_TOGGLED = 'totg',
    MSG_LAUNCH_CONFIG_WINDOW = 'lcfg',
    MSG_AUTORAISE_TOGGLED  = 'srdt',
    MSG_ALPHA_SLIDER_CHANGED = 'alsc',
    MSG_ICON_SIZE_CHANGED = 'isic'
};

// Unified configuration variable states
extern bool autoHideEnabled;
extern bool showSystemTray;
extern bool dockAlwaysOnTop;
extern bool fShowTitleOverlays;


struct TrackedWindowInfo {
    BString title;
    int32 windowIndex;
    BRect hitBox;

    // Explicit constructor to fix brace-enclosed initialization failures
    TrackedWindowInfo(BString t, int32 idx, BRect box) 
        : title(t), windowIndex(idx), hitBox(box) {}
};


struct LeafMenuArgs {
    HaikuGlDesktopEngine* engine;
    int32 winX;
    int32 winY;
    int32 mouseX;
    float currentDockH;
};


struct TrayItem {
    std::string name;
    int32 internalId;
    GLuint textureId;
    float currentRenderX; // Cached during Pass 2 for Mouse Click math!
    float currentRenderWidth;
};
std::vector<TrayItem> fLiveTrayItems;
bigtime_t fLastTrayUpdateTime = 0;

struct SystrayMenuArgs {
    HaikuGlDesktopEngine* engine;
    int32 winX;
    int32 winY;
    int32 mouseX;
    int32 mouseY;
    std::string itemName; // Safe isolated string copy
};

struct CpuMenuArgs {
    HaikuGlDesktopEngine* engine;
    int32 winX;
    int32 winY;
    int32 mouseX;
    int32 mouseY;
    float currentDockH;
};

struct HaikuRect {
    float left, top, right, bottom;    
    bool Contains(float x, float y) const {
        return (x >= left && x <= right && y >= top && y <= bottom);
    }    
    float Width() const { 
        return right - left; 
    }
};

struct HaikuPoint {
    float x;
    float y;
};

struct HaikuTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

struct BrowserFileItem {
    std::string name;
    HaikuTexture icon;
    HaikuTexture textTex;
    int textW = 0, textH = 0;
    HaikuRect clickBounds;
    bool isFolder;
    std::string fullPath;
};

struct TaskbarItem {
    std::string title;       
    std::string appName;     
    HaikuTexture icon;       
    bool isMinimized;       
    bool* openStateFlag;     
    bool* minimizeStateFlag; 
    team_id teamId;       
    int32 windowIndex;    
    float textAlpha = 0.0f;   
};

struct DesktopIconItem {
    std::string name;
    HaikuTexture texture;       // Core 48x48 icon file asset
    HaikuTexture textTexture;   // Dynamic text string label texture
    HaikuRect bounds;
    HaikuRect textBounds;       // Layout boundaries for label text box below the icon
    bool isFolder;
};





void GetTrackedWindowsFromTeam(team_id team, std::vector<TrackedWindowInfo>& outList) {
    outList.clear();

    app_info info;
    bool hasAppInfo = (be_roster->GetRunningAppInfo(team, &info) == B_OK);

    // 1. CRITICAL GUARD: Keep only the Rakarrack guard to prevent hard system freezes via FLTK
    if (hasAppInfo) {
        if (strcmp(info.signature, "application/x-vnd.rakarrack-haiku") == 0 || 
            BString(info.ref.name).ICompare("rakarrack") == 0) {
            outList.push_back(TrackedWindowInfo("Rakarrack", 0, BRect()));
            return;
        }
    }

    bool isTrackerApp = (hasAppInfo && (strcmp(info.signature, "application/x-vnd.Benjamin-TRAK") == 0 || 
                         strcmp(info.signature, "application/x-vnd.Be-TRAK") == 0));

    // 2. Query raw App Server window order stack directly
    int32 currentWorkspace = current_workspace(); 
    int32* windowTokens = nullptr;
    int32 totalWindows = 0;
    
    BPrivate::get_window_order(currentWorkspace, &windowTokens, &totalWindows);

    BString combinedPaths = "";
    int32 trackerValidCount = 0;

    if (windowTokens != nullptr && totalWindows > 0) {
        // Track the relative 0-indexed position of windows for each specific application team
        int32 appSpecificScriptIndex = 0; 

        for (int32 i = 0; i < totalWindows; ++i) {
            client_window_info* wInfo = get_window_info(windowTokens[i]);
            if (wInfo == nullptr) continue;

            if (wInfo->team == team) {
                BString subTitle(wInfo->name);
                
                if (subTitle.Length() > 0) {
                    
                    if (isTrackerApp) {
                        // FIX A: Detect and ignore the system background wallpaper layer
                        if ((subTitle == "Desktop" || subTitle.EndsWith("/Desktop")) && wInfo->feel == 1024) {
                            free(wInfo);
                            appSpecificScriptIndex++;
                            continue; 
                        }

                        // FIX B: Detect and ignore the background progress file dialog window 
                        if (subTitle == "Tracker status") {
                            free(wInfo);
                            appSpecificScriptIndex++;
                            continue; // Bypasses the status panel safely!
                        }

                        // Gather genuine folders into a combined layout horizontal line
                        if (trackerValidCount > 0) combinedPaths << " | ";
                        combinedPaths << subTitle;
                        trackerValidCount++;
                    } else {
                        // Standard apps: Keep your working multiline row entries completely untouched!
                        outList.push_back(TrackedWindowInfo(subTitle, appSpecificScriptIndex, BRect()));
                    }
                }
                // Always increment relative to the team's scriptable object bounds stack
                appSpecificScriptIndex++;
            }
            free(wInfo);
        }
        free(windowTokens);
    }

    // 3. Format final string output context
    if (isTrackerApp && trackerValidCount > 0) {
        BString finalDisplayString;
        finalDisplayString << "Tracker (" << combinedPaths << ")";
        // Target index 0 handles basic grouping for multi-window paths
        outList.push_back(TrackedWindowInfo(finalDisplayString, 0, BRect()));
    }

    // Ultimate fallback if no window fields whatsoever were populated by the loop pass
    if (outList.empty()) {
        BString fallbackName = (hasAppInfo && info.ref.name) ? info.ref.name : "Application";
        if (hasAppInfo) {
            if (strcmp(info.signature, "application/x-vnd.Be-TRAK") == 0) fallbackName = "Tracker";
            else if (strcmp(info.signature, "application/x-vnd.beunited.pe") == 0) fallbackName = "Pe";
        }
        outList.push_back(TrackedWindowInfo(fallbackName, 0, BRect()));
    }
}




void ActivateApplicationWindow(team_id team, int32 windowIndex) {
    BMessenger appMessenger(NULL, team);
    if (appMessenger.IsValid()) {
        // Build the precise script message that worked beautifully earlier
        BMessage activateMsg(B_SET_PROPERTY);
        activateMsg.AddSpecifier("Active");
        activateMsg.AddSpecifier("Window", windowIndex);
        activateMsg.AddBool("data", true);

        BMessage reply;
        appMessenger.SendMessage(&activateMsg, &reply, 20000, 20000);

        // --- NEW LINE: Unminimize the window if it's currently folded away ---
        BMessage unminimizeMsg(B_SET_PROPERTY);
        unminimizeMsg.AddSpecifier("Minimized");
        unminimizeMsg.AddSpecifier("Window", windowIndex);
        unminimizeMsg.AddBool("data", false); // Force Minimized to false
        
        BMessage unminimizeReply;
        appMessenger.SendMessage(&unminimizeMsg, &unminimizeReply, 20000, 20000);
    }
    
    // Globally lift the process context into the active foreground layer
    be_roster->ActivateApp(team);
}




using BPrivate::BNavMenu;
// =========================================================================
// PRIVATE SYMBOL ACCESS LAYER: UNNESTED PUBLIC FLOATING NAVIGATOR SUBCLASS
// =========================================================================
class BPopupNavMenu : public BPrivate::BNavMenu {
public:
    BPopupNavMenu(const char* title, uint32 message, const BMessenger& target)
        : BPrivate::BNavMenu(title, message, target) {}

    // Public bridge function to expose the protected base method to our click loop
    BMenuItem* PublicTrack() {
        return Track(true, nullptr);
    }
};



// =========================================================================
// CUSTOM RENDERING LAYER: LIVE GEOMETRIC REAL-TIME MEMORY USAGE GRAPH BAR
// =========================================================================
class BMemoryBarMenuItem : public BMenuItem {
public:
    BMemoryBarMenuItem(const char* label, double fillPercentage)
        : BMenuItem(label, nullptr), fFillPercentage(fillPercentage) {}

    void UpdateMetrics(double newPercentage, const char* newLabel) {
        fFillPercentage = newPercentage;
        SetLabel(newLabel);
    }

protected:
    virtual void GetContentSize(float* width, float* height) override {
        BMenuItem::GetContentSize(width, height);
        // FIXED: Expanded the total bounding box width parameter from 320px to 420px 
        // to give your long memory telemetry strings plenty of breathing room!
        *width = 420.0f;
        *height = 18.0f;
    }

    virtual void DrawContent() override {
        BMenu* menu = Menu();
        if (!menu) return;

        BRect bounds = Frame();
        float itemHeight = bounds.Height();
        
        font_height fh;
        menu->GetFontHeight(&fh);
        float fontBaseline = bounds.top + (itemHeight - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

        // FIXED: Pushed columns significantly rightward to completely eliminate text overlapping!
        float nameColumnLeft = bounds.left + 5.0f;   
        float textColumnLeft = bounds.left + 280.0f; // Shifted right by 100 pixels
        float barColumnLeft  = bounds.left + 330.0f; // Shifted right by 100 pixels
        float barWidth = 80.0f;

        // Render the descriptive text label
        menu->MovePenTo(nameColumnLeft, fontBaseline);
        menu->DrawString(Label());

        // Format and render the numeric consumption percentage string
        char pctStr[16];
        std::snprintf(pctStr, sizeof(pctStr), "%3.1f%%", fFillPercentage);
        menu->MovePenTo(textColumnLeft, fontBaseline);
        menu->DrawString(pctStr);

        // Build and draw the visual progress track capsule
        float barHeight = 10.0f;
        float barTop = bounds.top + (itemHeight - barHeight) / 2.0f;
        BRect barTrack(barColumnLeft, barTop, barColumnLeft + barWidth, barTop + barHeight);
        
        double clampedPercent = (fFillPercentage < 0.0) ? 0.0 : (fFillPercentage > 100.0) ? 100.0 : fFillPercentage;
        BRect fillCap(barColumnLeft, barTop, barColumnLeft + (barWidth * (clampedPercent / 100.0)), barTop + barHeight);

        // Dark track background silhouette
        menu->SetHighColor(45, 45, 45);
        menu->FillRect(barTrack);

        // Dynamic resource footprint styling thresholds
        if (fFillPercentage > 85.0) {
            menu->SetHighColor(220, 20, 60);    // Crimson Red
        } else if (fFillPercentage > 60.0) {
            menu->SetHighColor(255, 140, 0);   // Dark Orange
        } else {
            menu->SetHighColor(50, 205, 50);    // Neon Green matching your UI!
        }

        if (fillCap.Width() > 0) {
            menu->FillRect(fillCap);
        }

        // Apply a fine glass border overlay frame highlight
        menu->SetHighColor(90, 90, 90);
        menu->StrokeRect(barTrack);

        // Restore brush configurations back to standard text properties
        menu->SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
    }

private:
    double fFillPercentage;
};


// =========================================================================
// CUSTOM RENDERING LAYER: LIVE COLORED CPU PERFORMANCE GRAPH BAR + ICONS
// =========================================================================
class BCpuBarMenuItem : public BMenuItem {
public:
    BCpuBarMenuItem(const char* label, BMessage* message, double cpuPercent, BBitmap* icon = nullptr)
        : BMenuItem(label, message), fCpuPercent(cpuPercent), fIcon(icon) {}

    virtual ~BCpuBarMenuItem() override {
        delete fIcon; // Safely release bitmap memory when item is removed
    }

    void UpdateMetrics(double newPercent, const char* newLabel) {
        fCpuPercent = newPercent;
        SetLabel(newLabel);
    }

protected:
    virtual void GetContentSize(float* width, float* height) override {
        BMenuItem::GetContentSize(width, height);
        *width = 340.0f; // Expand width parameter slightly to house the icon space cleanly
        *height = 18.0f; 
    }

    virtual void DrawContent() override {
        BMenu* menu = Menu();
        if (!menu) return;

        BRect bounds = Frame();
        float itemHeight = bounds.Height();
        
        font_height fh;
        menu->GetFontHeight(&fh);
        float fontBaseline = bounds.top + (itemHeight - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

        // 1. Define our absolute, left-aligned column grid positions (offset to accommodate the icon)
        float iconColumnLeft = bounds.left + 5.0f;
        float nameColumnLeft = bounds.left + 25.0f;   // Shifted right by 20 pixels for clear layout padding
        float textColumnLeft = bounds.left + 200.0f; 
        float barColumnLeft = bounds.left + 250.0f;  
        float barWidth = 80.0f;

        // 2. Draw the application icon graphic if it was successfully resolved
        if (fIcon) {
            float graphicTop = bounds.top + (itemHeight - 16.0f) / 2.0f;
            menu->SetDrawingMode(B_OP_ALPHA);
            menu->DrawBitmap(fIcon, BPoint(iconColumnLeft, graphicTop));
            menu->SetDrawingMode(B_OP_COPY);
        }

        // 3. Render the Process Name string
        menu->MovePenTo(nameColumnLeft, fontBaseline);
        menu->DrawString(Label());

        // 4. Format and render the numeric percentage string
        char pctStr[16];
        std::snprintf(pctStr, sizeof(pctStr), "%3.1f%%", fCpuPercent);
        menu->MovePenTo(textColumnLeft, fontBaseline);
        menu->DrawString(pctStr);

        // 5. Render the graphical performance loading bar container
        float barHeight = 10.0f;
        float barTop = bounds.top + (itemHeight - barHeight) / 2.0f;
        BRect barTrack(barColumnLeft, barTop, barColumnLeft + barWidth, barTop + barHeight);
        
        double clampedPercent = (fCpuPercent < 0.0) ? 0.0 : (fCpuPercent > 100.0) ? 100.0 : fCpuPercent;
        BRect fillCap(barColumnLeft, barTop, barColumnLeft + (barWidth * (clampedPercent / 100.0)), barTop + barHeight);

        menu->SetHighColor(45, 45, 45);
        menu->FillRect(barTrack);

        if (fCpuPercent > 75.0) {
            menu->SetHighColor(220, 20, 60);    // Crimson Red
        } else if (fCpuPercent > 35.0) {
            menu->SetHighColor(255, 140, 0);   // Dark Orange
        } else {
            menu->SetHighColor(50, 205, 50);    // Neon Green
        }

        if (fillCap.Width() > 0) {
            menu->FillRect(fillCap);
        }

        menu->SetHighColor(90, 90, 90);
        menu->StrokeRect(barTrack);

        menu->SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
    }

private:
    double fCpuPercent;
    BBitmap* fIcon;
};



// =========================================================================
// LIVE-PULSING SUBSYSTEM: DETAILED MEMORY AND RAM CACHE PROFILER CASCADE
// =========================================================================
class BLiveMemoryMenu : public BMenu {
public:
    BLiveMemoryMenu(const char* title) : BMenu(title) {
        SetFlags(Flags() | B_PULSE_NEEDED);
    }

    virtual void AttachedToWindow() override {
        BMenu::AttachedToWindow();
        Window()->SetPulseRate(500000); // Pulse metrics smoothly every half second
    }

     virtual void Pulse() override {
        BMenu::Pulse();
        
        system_info info;
        if (get_system_info(&info) == B_OK) {
            double pageSize = static_cast<double>(B_PAGE_SIZE);
            double totalBytes = static_cast<double>(info.max_pages) * pageSize;
            double usedBytes = static_cast<double>(info.used_pages) * pageSize;
            
            int32 totalMB = static_cast<int32>(totalBytes / (1024.0 * 1024.0));
            int32 usedMB = static_cast<int32>(usedBytes / (1024.0 * 1024.0));
            int32 freeMB = totalMB - usedMB;

            // Compute actual global RAM consumption utilization scaling 
            double overallMemoryUsagePercent = 0.0;
            if (totalBytes > 0) {
                overallMemoryUsagePercent = (usedBytes / totalBytes) * 100.0;
            }

            // Create clean text strings
            char i1[64], i2[64], i3[64];
            std::snprintf(i1, sizeof(i1), "Used Physical Memory: %d MB", usedMB);
            std::snprintf(i2, sizeof(i2), "Free Available RAM: %d MB", freeMB);
            std::snprintf(i3, sizeof(i3), "Total Installed Capacity: %d MB", totalMB);

            // Update row item text metrics or inject custom green graph bar items seamlessly
            UpdateOrAddMemoryBarItem(0, i1, overallMemoryUsagePercent);
            UpdateOrAddMemoryBarItem(1, i2, 100.0 - overallMemoryUsagePercent); // Remaining percentage space
            UpdateOrAddMemoryBarItem(2, i3, 100.0); // Total capacity sits solid filled
        }
    }

private:
    void UpdateOrAddMemoryBarItem(int32 idx, const char* label, double percentage) {
        BMemoryBarMenuItem* item = dynamic_cast<BMemoryBarMenuItem*>(ItemAt(idx));
        if (item) {
            item->UpdateMetrics(percentage, label);
        } else {
            // Instantiate our brand new memory bar object class
            BMemoryBarMenuItem* newItem = new BMemoryBarMenuItem(label, percentage);
            AddItem(newItem);
        }
    }

};


class BRealtimeCpuMenu : public BMenu {
public:
    BRealtimeCpuMenu(const char* title) : BMenu(title) {
        SetFlags(Flags() | B_PULSE_NEEDED);
        
        system_info sysInfo;
        fCpuCount = (get_system_info(&sysInfo) == B_OK) ? sysInfo.cpu_count : 1;
        if (fCpuCount < 1) fCpuCount = 1;
        
        // Initialize our rolling tracking anchor time
        fLastUpdateTime = system_time();

        // =========================================================================
        // CRITICAL FIRST SWIPE FIX: POPULATE ALL TEAMS IMMEDIATELY ON CREATION!
        // This ensures the Haiku Window Layout Server sizes the menu correctly at launch.
        // =========================================================================
        team_info tInfo;
        int32 teamCookie = 0;
        int32 index = 0;

        while (get_next_team_info(&teamCookie, &tInfo) == B_OK) {
            if (tInfo.team <= 0 || std::strlen(tInfo.name) == 0) continue;

            char cleanName[B_OS_NAME_LENGTH];
            const char* lastSlash = std::strrchr(tInfo.name, '/');
            std::strncpy(cleanName, lastSlash ? lastSlash + 1 : tInfo.name, sizeof(cleanName));

            // Extract the tracker icon asset for the layout size framework on launch
            BBitmap* processIcon = nullptr;
            image_info imgInfo;
            int32 imgCookie = 0;
            if (get_next_image_info(tInfo.team, &imgCookie, &imgInfo) == B_OK) {
                BEntry appEntry(imgInfo.name);
                if (appEntry.Exists()) {
                    entry_ref ref;
                    if (appEntry.GetRef(&ref) == B_OK) {
                        BRect iconBounds(0, 0, 15, 15);
                        BBitmap* tempIcon = new BBitmap(iconBounds, B_RGBA32);
                        if (BNodeInfo::GetTrackerIcon(&ref, tempIcon, B_MINI_ICON) == B_OK) {
                            processIcon = tempIcon;
                        } else {
                            delete tempIcon;
                        }
                    }
                }
            }

            thread_info thInfo;
            int32 thCookie = 0;
            bigtime_t currentTeamTotalTime = 0;
            while (get_next_thread_info(tInfo.team, &thCookie, &thInfo) == B_OK) {
                currentTeamTotalTime += thInfo.user_time + thInfo.kernel_time;
            }

            fProcessHistoryMap[tInfo.team].mainThreadId = 0;
            fProcessHistoryMap[tInfo.team].lastTimeSample = currentTeamTotalTime;

            BMessage* killThMsg = new BMessage('kthr');
            killThMsg->AddInt32("target_thread", tInfo.team);
            killThMsg->AddString("target_name", cleanName);
            
            // Pass the icon to initialize item sizes perfectly on swipe one
            AddItem(new BCpuBarMenuItem(cleanName, killThMsg, 0.0, processIcon));
            
            index++;
            if (index >= 45) break;
        }

    }


    virtual void AttachedToWindow() override {
        BMenu::AttachedToWindow();
        Window()->SetPulseRate(200000); // Pulse every 200ms
    }

    virtual void Pulse() override;

private:
    int32 fCpuCount;
    bigtime_t fLastUpdateTime;

    // Persistent cache structure to measure metrics across separate pulses
    struct CachedProcessState {
        thread_id mainThreadId;
        bigtime_t lastTimeSample;
    };
    std::map<team_id, CachedProcessState> fProcessHistoryMap;
};


void BRealtimeCpuMenu::Pulse() {
    BMenu::Pulse();

    // Compute global time differences since the absolute last frame slice
    bigtime_t currentTime = system_time();
    bigtime_t totalTimeDelta = (currentTime - fLastUpdateTime) * fCpuCount;
    fLastUpdateTime = currentTime; // Roll anchor forward

    // Local snapshot buffer for this specific layout pass
    struct DisplayElement {
        team_id teamId;
        double calculatedCpu;
        char name[B_OS_NAME_LENGTH];
    };
    std::vector<DisplayElement> currentPassList;

    team_info tInfo;
    int32 teamCookie = 0;

    // 2. Iterate across the flat system team table directly to capture ALL running apps
    while (get_next_team_info(&teamCookie, &tInfo) == B_OK) {
        if (tInfo.team <= 0 || std::strlen(tInfo.name) == 0) continue;

        DisplayElement element;
        element.teamId = tInfo.team;
        element.calculatedCpu = 0.0; // Default fallback for newly discovered processes

        const char* lastSlash = std::strrchr(tInfo.name, '/');
        std::strncpy(element.name, lastSlash ? lastSlash + 1 : tInfo.name, sizeof(element.name));

        // Loop through ALL threads belonging to this team and sum their runtimes
        thread_info thInfo;
        int32 thCookie = 0;
        bigtime_t currentTeamTotalTime = 0;

        while (get_next_thread_info(tInfo.team, &thCookie, &thInfo) == B_OK) {
            currentTeamTotalTime += thInfo.user_time + thInfo.kernel_time;
        }

        // Compute performance deltas against team histories
        if (fProcessHistoryMap.find(tInfo.team) != fProcessHistoryMap.end()) {
            bigtime_t oldTimeSample = fProcessHistoryMap[tInfo.team].lastTimeSample;
            if (totalTimeDelta > 0 && currentTeamTotalTime >= oldTimeSample) {
                element.calculatedCpu = (static_cast<double>(currentTeamTotalTime - oldTimeSample) / 
                                         static_cast<double>(totalTimeDelta)) * 100.0;
            }
        }
        
        // Cache this team's total aggregated time for the next pulse calculation
        fProcessHistoryMap[tInfo.team].mainThreadId = 0; 
        fProcessHistoryMap[tInfo.team].lastTimeSample = currentTeamTotalTime;

        // FIXED: Always include the running process immediately so it never gets dropped!
        currentPassList.push_back(element);
    }

    // 3. Purge history map records for applications that exited entirely
    auto mapIter = fProcessHistoryMap.begin();
    while (mapIter != fProcessHistoryMap.end()) {
        bool stillExists = false;
        for (const auto& live : currentPassList) {
            if (live.teamId == mapIter->first) {
                stillExists = true;
                break;
            }
        }
        if (!stillExists) {
            mapIter = fProcessHistoryMap.erase(mapIter);
        } else {
            ++mapIter;
        }
    }

    // 4. Update existing graphical bars or create new ones complete with system icons!
    int32 index = 0;
    for (const auto& entry : currentPassList) {
        char rowText[B_OS_NAME_LENGTH + 16];
        std::snprintf(rowText, sizeof(rowText), "%s", entry.name);

        BCpuBarMenuItem* item = dynamic_cast<BCpuBarMenuItem*>(ItemAt(index));
        if (item) {
            item->UpdateMetrics(entry.calculatedCpu, rowText);
            if (item->Message()) {
                item->Message()->ReplaceInt32("target_thread", entry.teamId);
            }
        } else {
            // Locate and extract the dynamic system icon for this newly listed process team
            BBitmap* processIcon = nullptr;
            image_info imgInfo;
            int32 imgCookie = 0;
            
            if (get_next_image_info(entry.teamId, &imgCookie, &imgInfo) == B_OK) {
                BEntry appEntry(imgInfo.name);
                if (appEntry.Exists()) {
                    entry_ref ref;
                    if (appEntry.GetRef(&ref) == B_OK) {
                        BRect iconBounds(0, 0, 15, 15);
                        BBitmap* tempIcon = new BBitmap(iconBounds, B_RGBA32);
                        
                        if (BNodeInfo::GetTrackerIcon(&ref, tempIcon, B_MINI_ICON) == B_OK) {
                            processIcon = tempIcon; // Successfully grabbed the icon asset!
                        } else {
                            delete tempIcon;
                        }
                    }
                }
            }

            // Configure the message tracking hooks
            BMessage* killThMsg = new BMessage('kthr');
            killThMsg->AddInt32("target_thread", entry.teamId);
            killThMsg->AddString("target_name", entry.name);
            
            AddItem(new BCpuBarMenuItem(rowText, killThMsg, entry.calculatedCpu, processIcon));
        }
        
        index++;
        if (index >= 45) break;
    }


    // Trim trailing elements smoothly
    while (CountItems() > index) {
        delete RemoveItem(index);
    }

    Invalidate();
}


// =========================================================================
// CUSTOM RENDERING LAYER: QUIT APPLICATION SUBMENU ITEMS (ICON ALIGNED)
// =========================================================================
class BIconMenuItem : public BMenuItem {
public:
    // Overload 1: For Cascading Submenus (Teams)
    BIconMenuItem(BMenu* submenu, BBitmap* icon = nullptr)
        : BMenuItem(submenu), fIcon(icon) {}

    // Overload 2: For Standard Action Items (Quit Processes)
    BIconMenuItem(const char* label, BMessage* message, BBitmap* icon = nullptr)
        : BMenuItem(label, message), fIcon(icon) {}

    virtual ~BIconMenuItem() override {
        delete fIcon; // Safely release bitmap memory upon closure
    }

protected:
    virtual void GetContentSize(float* width, float* height) override {
        BMenuItem::GetContentSize(width, height);
        // Force uniform structural row boundaries matching our design standards
        *width = 240.0f; 
        *height = 18.0f; 
    }

    virtual void DrawContent() override {
        BMenu* menu = Menu();
        if (!menu) return;

        BRect bounds = Frame();
        float itemHeight = bounds.Height();

        // 1. Calculate font metrics for perfect vertical centering inside the row box
        font_height fh;
        menu->GetFontHeight(&fh);
        float fontBaseline = bounds.top + (itemHeight - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

        // 2. Define clear absolute column grids for your elements
        float iconColumnLeft = bounds.left + 5.0f;
        float nameColumnLeft = bounds.left + 25.0f; // Left padding gap to separate text from icons

        // 3. Render the application binary icon graphic (perfectly centered vertically)
        if (fIcon) {
            float graphicTop = bounds.top + (itemHeight - 16.0f) / 2.0f;
            menu->SetDrawingMode(B_OP_ALPHA);
            menu->DrawBitmap(fIcon, BPoint(iconColumnLeft, graphicTop));
            menu->SetDrawingMode(B_OP_COPY);
        }

        // 4. FIXED: Render the process text label manually.
        // Skipping BMenuItem::DrawContent completely prevents Haiku from shifting our text unaligned!
        menu->SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
        menu->MovePenTo(nameColumnLeft, fontBaseline);
        menu->DrawString(Label());
    }

private:
    BBitmap* fIcon;
};




// Streamlined structural definition block
class AsyncCpuMenuRunner : public BWindow {
public:
    AsyncCpuMenuRunner(CpuMenuArgs* args)
        : BWindow(BRect(-50, -50, -10, -10), "AsyncCpuMenuLooper", B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, 0),
          fArgs(args)
    {
        BView* dummyView = new BView(Bounds(), "dummy", B_FOLLOW_ALL, B_WILL_DRAW);
        AddChild(dummyView);
        
        Run();
        PostMessage(MSG_LAUNCH_MENU);
    }

    virtual void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case MSG_LAUNCH_MENU:
                _DisplayCPUGraphMenu(); // Will be resolved downstream
                Quit(); 
                break;
            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

private:
    enum { MSG_LAUNCH_MENU = 'lmnc' };
    void _DisplayCPUGraphMenu(); // Declaration only!

    CpuMenuArgs* fArgs;
};





void SyncDynamicSystrayTextures() {
    bigtime_t currentTime = system_time();
    // Throttle: Only probe Deskbar twice per second (500,000 microseconds)
    if (currentTime - fLastTrayUpdateTime < 500000) {
        return; 
    }
    fLastTrayUpdateTime = currentTime;

    BDeskbar deskbarControl;
    std::vector<std::pair<std::string, int32>> activeSnapshot;

    // =========================================================================
    // VERIFIED SYSTEM METHOD: CAPTURE RUNNING APPS VIA THE STABLE ROSTER PROBER
    // =========================================================================
    for (int32 idProber = 0; idProber < 150; ++idProber) {
        const char* foundName = nullptr;
        if (deskbarControl.GetItemInfo(idProber, &foundName) == B_OK) {
            if (foundName != nullptr && strlen(foundName) > 0) {
                activeSnapshot.push_back({std::string(foundName), idProber});
            }
        }
    }

    // 2. Diff check: Rebuild GL textures if a structural change occurred
    bool structureChanged = (activeSnapshot.size() != fLiveTrayItems.size());
    if (!structureChanged) {
        for (size_t i = 0; i < activeSnapshot.size(); ++i) {
            if (activeSnapshot[i].first != fLiveTrayItems[i].name) {
                structureChanged = true;
                break;
            }
        }
    }

    // 3. Rebuild GL textures using verified 16x16 -> 32x32 upscale matrix mechanics
    if (structureChanged) {
        // Free old GL texture allocations cleanly out of GPU VRAM
        for (auto& oldItem : fLiveTrayItems) {
            if (oldItem.textureId != 0) {
                glDeleteTextures(1, &oldItem.textureId);
            }
        }
        fLiveTrayItems.clear();

        BRect nativeBounds(0, 0, 15, 15);
        int32 destSize = 32; 

        for (const auto& snap : activeSnapshot) {
            BBitmap scratchBitmap(nativeBounds, B_RGBA32);
            entry_ref appRef;
            bool assetResolved = false;

            BString itemName(snap.first.c_str());
            BString signature = "application/x-vnd.Haiku-";
            
            if (itemName == "ProcessControllerView" || itemName == "ProcessController") {
                itemName = "ProcessController";
                signature = "application/x-vnd.Haiku-ProcessController";
            } else if (itemName == "MediaReplicant") {
                itemName = "Media"; 
                signature = "application/x-vnd.Haiku-MediaPreferences";
            } else if (itemName == "NetworkStatus") {
                signature = "application/x-vnd.Haiku-NetworkStatus";
            } else if (itemName == "SuperMusicTrayIcon") {
                // Configured perfectly for your unique system music app environment layout
                itemName = "HaikuSuperMusicThingy"; 
                signature = "application/x-vnd.HaikuSuperMusicThingy"; 
            } else {
                signature << itemName;
            }

            if (be_roster->FindApp(signature.String(), &appRef) == B_OK) {
                assetResolved = true;
            } else {
                const char* fallbackDirectories[] = {
                    "/boot/system/apps",
                    "/boot/system/preferences"  
                };

                for (const char* dir : fallbackDirectories) {
                    BPath pathProber(dir);
                    pathProber.Append(itemName.String());
                    BEntry checkEntry(pathProber.Path());
                    if (checkEntry.Exists()) {
                        checkEntry.GetRef(&appRef);
                        assetResolved = true;
                        break;
                    }
                }
            }

            GLuint textureID = 0;

            if (assetResolved) {
                BNode fileNode(&appRef);
                BNodeInfo nodeInfo(&fileNode);
                
                if (nodeInfo.GetTrackerIcon(&scratchBitmap, B_MINI_ICON) == B_OK) {
                    uint32* srcPtr = (uint32*)scratchBitmap.Bits();

                    if (srcPtr != nullptr) {
                        std::vector<uint32> highDefBuffer(destSize * destSize, 0x00000000);
                        uint32* destPtr = highDefBuffer.data();

                        for (int y = 0; y < destSize; ++y) {
                            for (int x = 0; x < destSize; ++x) {
                                int srcX = (x * 16) / destSize;
                                int srcY = (y * 16) / destSize;
                                destPtr[y * destSize + x] = srcPtr[srcY * 16 + srcX];
                            }
                        }

                        glGenTextures(1, &textureID);
                        glBindTexture(GL_TEXTURE_2D, textureID);

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                        glTexImage2D(
                            GL_TEXTURE_2D, 0, GL_RGBA8, 
                            destSize, destSize, 0, 
                            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 
                            highDefBuffer.data()
                        );

                        glBindTexture(GL_TEXTURE_2D, 0);
                    }
                }
            }

            // ALWAYS PUSH TARGET INTO THE REGISTRY ARRAY EVEN IF TEXTURE IS 0!
            // This balances array sizes perfectly, preventing the infinite reallocation loop.
            TrayItem newItem;
            newItem.name = snap.first;
            newItem.internalId = snap.second;
            newItem.textureId = textureID; 
            newItem.currentRenderX = 0.0f;
            newItem.currentRenderWidth = 0.0f;
            fLiveTrayItems.push_back(newItem);
        }
    }
}


class ConfigView : public BView {
private:
    BCheckBox* fAutoHideCheckbox;
    BCheckBox* fSystemTrayCheckbox;
    BCheckBox* fAutoRaiseCheckbox;
    BCheckBox* fTextOverlaysCheckbox;
    BSlider*   fAlphaSlider; 
    BSlider*   fIconSizeSlider; 
    BButton*   fAboutButton; 

public:

    ConfigView(BRect frame) : BView(frame, "ConfigView", B_FOLLOW_ALL, B_WILL_DRAW) {
        SetViewColor(rgb_color{24, 24, 28, 255});
		/*
        // 1. Define the "About hdesktop" Button Layout (Y: 82 to 106)
        BRect aboutBtnRect(25.0f, 82.0f, frame.Width() - 25.0f, 106.0f);
        fAboutButton = new BButton(aboutBtnRect, "about_btn", "About hdesktop", new BMessage('abou'));
        AddChild(fAboutButton);
		*/
        // 2. FIXED CLIPPING: Inward margins expanded from 25.0f to 35.0f to wrap inside the tray borders beautifully!
        // Row 1: Shifted lower to match the expanded tray offset (Y: 135)
        BRect checkboxRect(35.0f, 135.0f, frame.Width() - 35.0f, 150.0f);
        fAutoHideCheckbox = new BCheckBox(checkboxRect, "auto_hide_cb", "Enable Auto-Hide", 
            new BMessage(MSG_AUTOHIDE_TOGGLED));
        fAutoHideCheckbox->SetHighColor(rgb_color{240, 240, 240, 255}); 
        fAutoHideCheckbox->SetValue(autoHideEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fAutoHideCheckbox);

        // Row 2
        BRect trayCheckboxRect(35.0f, 155.0f, frame.Width() - 35.0f, 170.0f);
        fSystemTrayCheckbox = new BCheckBox(trayCheckboxRect, "sys_tray_cb", "Enable System Tray", 
            new BMessage(MSG_SYSTEMTRAY_TOGGLED));
        fSystemTrayCheckbox->SetHighColor(rgb_color{240, 240, 240, 255});
        fSystemTrayCheckbox->SetValue(showSystemTray ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fSystemTrayCheckbox);

        // Row 3
        BRect autoRaiseRect(35.0f, 175.0f, frame.Width() - 35.0f, 190.0f);
        fAutoRaiseCheckbox = new BCheckBox(autoRaiseRect, "auto_raise_cb", "Enable Auto-Raise", 
            new BMessage(MSG_AUTORAISE_TOGGLED));
        fAutoRaiseCheckbox->SetHighColor(rgb_color{240, 240, 240, 255});
        fAutoRaiseCheckbox->SetValue(dockAlwaysOnTop ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fAutoRaiseCheckbox);

        // Row 4
        BRect textOverlaysRect(35.0f, 195.0f, frame.Width() - 35.0f, 210.0f);
        fTextOverlaysCheckbox = new BCheckBox(textOverlaysRect, "text_overlays_cb", "Enable Application Title Overlays", 
            new BMessage(MSG_TEXTOVERLAYS_TOGGLED));
        fTextOverlaysCheckbox->SetHighColor(rgb_color{220, 225, 235, 255}); 
        fTextOverlaysCheckbox->SetValue(fShowTitleOverlays ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fTextOverlaysCheckbox);
        
        // Transparency Slider Row
        BRect sliderRect(35.0f, 225.0f, frame.Width() - 35.0f, 265.0f);
        fAlphaSlider = new BSlider(sliderRect, "alpha_slider", "Dock Transparency", 
            new BMessage(MSG_ALPHA_SLIDER_CHANGED), 0, 100);
        fAlphaSlider->SetHighColor(rgb_color{220, 225, 235, 255});
        fAlphaSlider->SetLimitLabels("Transparent", "Opaque");
        fAlphaSlider->SetValue(static_cast<int32>(fDockAlpha * 100.0f));
        AddChild(fAlphaSlider);

        // Icon Size Slider Row
        BRect sizeSliderRect(35.0f, 285.0f, frame.Width() - 35.0f, 325.0f);
        fIconSizeSlider = new BSlider(sizeSliderRect, "size_slider", "Icon Size", 
            new BMessage(MSG_ICON_SIZE_CHANGED), 32, 72);
        fIconSizeSlider->SetHighColor(rgb_color{220, 225, 235, 255});
        fIconSizeSlider->SetLimitLabels("Small", "Large");
        fIconSizeSlider->SetValue(static_cast<int32>(fBaseIconSize));
        AddChild(fIconSizeSlider);
    }





    virtual void Draw(BRect updateRect) {
        float canvasWidth = Bounds().Width();

        // 1. Render Window Header Context Title
        SetFont(be_bold_font);
        SetFontSize(14.0f);
        SetHighColor(rgb_color{220, 225, 235, 255});
        
        BString headerStr("hdesktop settings");
        float titleWidth = StringWidth(headerStr.String());
        DrawString(headerStr.String(), BPoint((canvasWidth - titleWidth) / 2.0f, 25.0f));

        // 2. Draw Decorative Header Separator line
        SetHighColor(rgb_color{50, 52, 60, 255}); 
        StrokeLine(BPoint(15.0f, 35.0f), BPoint(canvasWidth - 15.0f, 35.0f));

        // Get live mouse position for interactive buttons
        BPoint cursorPoint;
        uint32 transitButtons;
        GetMouse(&cursorPoint, &transitButtons, false);
        SetDrawingMode(B_OP_ALPHA);

        // 3. Define the Interactive "Shutdown hDesktop" Button Metrics (COMPRESSED)
        BRect shutdownBtnRect(25.0f, 50.0f, canvasWidth - 25.0f, 74.0f);
        
        if (shutdownBtnRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{220, 60, 60, 45});
            FillRect(shutdownBtnRect);
            SetHighColor(rgb_color{255, 90, 90, 255}); 
        } else {
            SetHighColor(rgb_color{35, 36, 42, 255}); 
            FillRect(shutdownBtnRect);
            SetHighColor(rgb_color{210, 100, 100, 255}); 
        }
        StrokeRect(shutdownBtnRect);
        
        SetFont(be_bold_font);
        SetFontSize(12.0f);
        BString shutdownText("Shutdown hDesktop");
        float shutdownTextW = StringWidth(shutdownText.String());
        DrawString(shutdownText.String(), BPoint(shutdownBtnRect.left + (shutdownBtnRect.Width() - shutdownTextW) / 2.0f, 66.0f));
        
        

        // 4. Define the "About hdesktop" Button Layout (Y: 82 to 106)
        BRect aboutBtnRect(25.0f, 82.0f, canvasWidth - 25.0f, 106.0f);
        
        if (aboutBtnRect.Contains(cursorPoint)) {
            // Hover state: Muted blue background (matching the 45 alpha layout) and bright blue text
            SetHighColor(rgb_color{60, 90, 220, 45});
            FillRect(aboutBtnRect);
            SetHighColor(rgb_color{120, 150, 255, 255}); // Bright hover blue
        } else {
            // Normal state: Dark background and standard medium blue text
            SetHighColor(rgb_color{35, 36, 42, 255}); 
            FillRect(aboutBtnRect);
            SetHighColor(rgb_color{90, 110, 210, 255});  // Standard medium blue
        }
        StrokeRect(aboutBtnRect);


        
        // 5. Draw the Text centered inside the button
        SetFont(be_bold_font);
        SetFontSize(12.0f);
        BString aboutText("About hdesktop");
        float aboutTextW = StringWidth(aboutText.String());
        // Baseline calculated at Y: 98.0f to center 12px font inside 82-106 bounds
        DrawString(aboutText.String(), BPoint(aboutBtnRect.left + (aboutBtnRect.Width() - aboutTextW) / 2.0f, 98.0f));
  

        // 6. BALANCED BACKING CONTAINER (FULLY EXTENDED BOTTOM STRIDE)
        // FIXED: Pushed bottom coordinate from 335.0f down to 355.0f so it 
        // completely wraps the "Small" and "Large" text labels perfectly!
        SetHighColor(rgb_color{30, 31, 37, 255}); 
        BRect checkboxTrayRect(20.0f, 120.0f, canvasWidth - 20.0f, 355.0f);
        FillRoundRect(checkboxTrayRect, 4.0f, 4.0f);
        SetHighColor(rgb_color{48, 50, 58, 255});
        StrokeRoundRect(checkboxTrayRect, 4.0f, 4.0f);

        // 7. Standard Window Control "Close" button tracking metrics at footer
        // Shifted downward below the fully wrapped tray baseline (Y: 370 to 395)
        BRect closeBtnRect((canvasWidth - 100.0f) / 2.0f, 370.0f, 
                           (canvasWidth + 100.0f) / 2.0f, 395.0f);
        
        if (closeBtnRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{100, 120, 160, 45});
            FillRect(closeBtnRect);
            SetHighColor(rgb_color{140, 175, 230, 255});
        } else {
            SetHighColor(rgb_color{40, 42, 48, 255});
            FillRect(closeBtnRect);
            SetHighColor(rgb_color{150, 160, 175, 255});
        }
        StrokeRect(closeBtnRect);
        
        BString btnText("close");
        float btnTextW = StringWidth(btnText.String());
        DrawString(btnText.String(), BPoint((canvasWidth - btnTextW) / 2.0f, 387.0f));


    }


    virtual void MouseMoved(BPoint point, uint32 transit, const BMessage* message) {
        Invalidate(); 
    }

    virtual void MouseDown(BPoint point) {
        float canvasWidth = Bounds().Width();
        
        BRect shutdownBtnRect(25.0f, 50.0f, canvasWidth - 25.0f, 74.0f);
        
        // Match Layout Calibration: Y: 82 to 106 (Synchronized with your Draw location)
        BRect aboutBtnRect(25.0f, 82.0f, canvasWidth - 25.0f, 106.0f);

        // MATCH LAYOUT CALIBRATION: Synchronized with your new button position (Y: 370 to 395)
        BRect closeBtnRect((canvasWidth - 100.0f) / 2.0f, 370.0f, 
                           (canvasWidth + 100.0f) / 2.0f, 395.0f);

        // Check if Shutdown button was clicked
        if (shutdownBtnRect.Contains(point)) {
            if (be_app) {
                be_app->PostMessage(B_QUIT_REQUESTED);
            }
            return;
        }

        // Check if About button was clicked
        if (aboutBtnRect.Contains(point)) {
            if (Window()) {
                // Posts the 'abou' message back to this view's handler to pop the window open
                Window()->PostMessage('abou', this);
            }
            return;
        }

        // Check if Close button was clicked
        if (closeBtnRect.Contains(point)) {
            if (Window()) {
                Window()->Quit(); 
            }
            return;
        }
        
        // Pass unhandled clicks down to the base class
        BView::MouseDown(point);
    }




    
    virtual void AttachedToWindow() {
        BView::AttachedToWindow();
        fAutoHideCheckbox->SetTarget(this);
        fSystemTrayCheckbox->SetTarget(this);
        fAutoRaiseCheckbox->SetTarget(this);
        fTextOverlaysCheckbox->SetTarget(this);
        fAlphaSlider->SetTarget(this); 
        fIconSizeSlider->SetTarget(this); 
       // fAboutButton->SetTarget(this); 
    }

    virtual void MessageReceived(BMessage* message) {
        switch (message->what) {
            case MSG_AUTOHIDE_TOGGLED: {
                autoHideEnabled = (fAutoHideCheckbox->Value() == B_CONTROL_ON);
                SaveConfiguration();
                break;
            }

            case MSG_SYSTEMTRAY_TOGGLED: {
                showSystemTray = (fSystemTrayCheckbox->Value() == B_CONTROL_ON);
                SaveConfiguration(); 
                break;
            }
            
            case MSG_AUTORAISE_TOGGLED: {
                dockAlwaysOnTop = (fAutoRaiseCheckbox->Value() == B_CONTROL_ON);
                SaveConfiguration();
                Invalidate();
                break;
            }

            case MSG_TEXTOVERLAYS_TOGGLED: {
                fShowTitleOverlays = (fTextOverlaysCheckbox->Value() == B_CONTROL_ON);
                SaveConfiguration();
                Invalidate();
                break;
            }
            
             case MSG_ALPHA_SLIDER_CHANGED: {
                fDockAlpha = fAlphaSlider->Value() / 100.0f;
                SaveConfiguration();                
                Invalidate();
                break;
            }
            
            
            case MSG_ICON_SIZE_CHANGED: {
                fBaseIconSize = static_cast<float>(fIconSizeSlider->Value());
                SaveConfiguration();                
                Invalidate();
                break;
            }
            
            case 'abou': {
            	
                BAlert* aboutAlert = new BAlert("About hdesktop",
                    "hdesktop SDL Dock\n"
                    "MIT License\n"
                    "Version v1.0.34\n"
                    "(c) 2026 ablyss\n\n"
                    
                    "Enjoy!\n\n"
                    
                    "",
                    "Awesome!", nullptr, nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);
                aboutAlert->Go(); 
                break;
            }


            
            default:
                BView::MessageReceived(message);
                break;
        }
    }


};



// =========================================================================
// NATIVE CONFIGURATION MANAGEMENT BWINDOW OVERLAY
// =========================================================================
class HaikuConfigWindow : public BWindow {
public:
	HaikuConfigWindow(BRect centralAnchor)
	    : BWindow(BRect(0, 0, 560, 450), "hdesktop Configuration",
	              B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, 
	              B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE) {
	    
	    ResizeTo(560.0f, 450.0f);
	    float targetX = centralAnchor.left + (centralAnchor.Width() - 560.0f) / 2.0f;
	    float targetY = centralAnchor.top + (centralAnchor.Height() - 450.0f) / 2.0f;
	    MoveTo(targetX, targetY);
	    
	    ConfigView* configView = new ConfigView(Bounds());
	    AddChild(configView);
	}




    virtual ~HaikuConfigWindow() {
        gActiveConfigInstance = nullptr; // Reset address register safely on destruction
    }
};


// =========================================================================
// NATIVE BVIEW CARD GRID HOLDER (FULL WIDTH PROFILE)
// =========================================================================
class DrawerView : public BView {
public:
    struct DrawerItem {
        BString    name;
        entry_ref  ref;
        BBitmap*   icon;
    };

    BList fItemsList;

    DrawerView(BRect frame) : BView(frame, "DrawerView", B_FOLLOW_ALL, B_WILL_DRAW) {
        // Set a sleek matte background color that coordinates with your dark desktop setup
        SetViewColor(rgb_color{24, 24, 28, 255}); 
        ScanSystemDirectories();
    }

    ~DrawerView() {
        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            DrawerItem* item = (DrawerItem*)fItemsList.ItemAt(i);
            if (item) {
                delete item->icon;
                delete item;
            }
        }
    }
    
    virtual void MouseMoved(BPoint point, uint32 transit, const BMessage* message) {
        // If the mouse left this view container, double check if it left the window
        if (transit == B_EXITED_VIEW && Window()) {
            BPoint screenPoint = ConvertToScreen(point);
            if (!Window()->Frame().Contains(screenPoint)) {
                Window()->Quit();
                return;
            }
        }
        // Force the app cell grid canvas to instantly refresh as your cursor glides across choices
        Invalidate(); 
    }


    static bool CompareDrawerItems(const DrawerItem* a, const DrawerItem* b) {
        BString nameA(a->name);
        BString nameB(b->name);
        return nameA.ICompare(nameB) < 0;
    }
    
    void KeyDown(const char* bytes, int32 numBytes)
	{
	    if (numBytes == 1) {
	        // Intercept standard ascii / character byte maps
	        switch (bytes[0]) {
	            case B_ESCAPE:
	            case B_SPACE:
	            {
	                if (Window()) {
	                    Window()->Quit();
	                }
	                return;
	            }
	        }
	    }
	    
	    // Pass any other keystrokes up to the base class handler loop safely
	    BView::KeyDown(bytes, numBytes);
	}

    
    
	void ProcessAndAddItem(BEntry& entry, const char* overrideName, std::vector<DrawerItem*>& targetVector, BEntry* parentEntry = nullptr) {
	    char name[B_FILE_NAME_LENGTH];
	    if (overrideName != nullptr) {
	        strncpy(name, overrideName, B_FILE_NAME_LENGTH);
	    } else {
	        if (entry.GetName(name) != B_OK) return;
	    }
	
	    DrawerItem* item = new DrawerItem();
	    item->name = name;
	    entry.GetRef(&item->ref);
	    
	    item->icon = new BBitmap(BRect(0, 0, 47, 47), B_RGBA32);
	    bool iconLoaded = false;
	    
	    // 1. Try to read the native icon from the executable binary itself
	    BNodeInfo nodeInfo;
	    BNode node(&entry);
	    if (nodeInfo.SetTo(&node) == B_OK) {
	        if (nodeInfo.GetIcon(item->icon, B_LARGE_ICON) == B_OK) {
	            iconLoaded = true;
	        }
	    }
	
	    // --- NEW FALLBACK BLOCK: Fall back to the parent folder's icon if the binary has none ---
	    if (!iconLoaded && parentEntry != nullptr) {
	        BNode parentNode(parentEntry);
	        BNodeInfo parentNodeInfo(&parentNode);
	        if (parentNodeInfo.InitCheck() == B_OK) {
	            if (parentNodeInfo.GetIcon(item->icon, B_LARGE_ICON) == B_OK) {
	                iconLoaded = true;
	            }
	        }
	    }
	
	    // 2. Generic System Asset Fallback
	    if (!iconLoaded) {
	        BMimeType genericMime("application/octet-stream");
	        if (genericMime.InitCheck() != B_OK || genericMime.GetIcon(item->icon, B_LARGE_ICON) != B_OK) {
	            delete item->icon;
	            item->icon = nullptr;
	        }
	    }
	    
	    targetVector.push_back(item);
	}



	void ScanSystemDirectories() {
	    std::vector<DrawerItem*> temporarySortedVector;
	    const char* paths[] = { "/boot/system/apps", "/boot/system/demos", "/boot/system/preferences" };
	    
	    for (int p = 0; p < 3; p++) {
	        BDirectory dir(paths[p]);
	        if (dir.InitCheck() != B_OK) continue;
	
	        BEntry entry;
	        while (dir.GetNextEntry(&entry) == B_OK) {
	            if (entry.IsDirectory()) {
	                char folderName[B_FILE_NAME_LENGTH];
	                if (entry.GetName(folderName) != B_OK) continue;
	
	                BDirectory subDir(&entry);
	                if (subDir.InitCheck() == B_OK) {
	                    BEntry subEntry;
	                    
	                    // Track choices across the folder inspection sweep
	                    BEntry bestAppEntry;
	                    char bestAppName[B_FILE_NAME_LENGTH] = {0};
	                    while (subDir.GetNextEntry(&subEntry) == B_OK) {
	                        char subName[B_FILE_NAME_LENGTH];
	                        if (subEntry.GetName(subName) != B_OK) continue;
	
	                        BNode subNode(&subEntry);
	                        BNodeInfo subNodeInfo(&subNode);
	                        char mimeType[B_MIME_TYPE_LENGTH] = {0};
	                        subNodeInfo.GetType(mimeType);
	
	                        // Is this an executable file or a wrapper matching the folder metadata name?
	                        if (strcmp(mimeType, "application/x-vnd.Be-elfexecutable") == 0 ||
	                            strcmp(mimeType, "text/x-source-code") == 0 || // Catch shell script wrappers
	                            strstr(subName, folderName) != nullptr) {
	                            
	                            // Check if this specific item contains the vector icon asset
	                            attr_info attrInfo;
								if (subNode.GetAttrInfo("BEOS:ICON", &attrInfo) == B_OK) {
								    bestAppEntry = subEntry;
								    strncpy(bestAppName, subName, B_FILE_NAME_LENGTH);
								    break; 
								}                            
	                            // Backup: Save the first binary we encounter if no custom icon wrapper presents itself
	                            if (bestAppName[0] == '\0') {
	                                bestAppEntry = subEntry;
	                                strncpy(bestAppName, subName, B_FILE_NAME_LENGTH);
	                            }
	                        }
	                    }
	
	                    // If we found a valid launcher target inside the folder, register it!
	                    if (bestAppName[0] != '\0') {
	                        ProcessAndAddItem(bestAppEntry, bestAppName, temporarySortedVector, &entry);
	                    } else {
	                        // Fallback: Show the parent folder itself if completely empty of executables
	                        ProcessAndAddItem(entry, folderName, temporarySortedVector);
	                    }
	                }
	            } 
	            else {
	                // Top level system preferences and apps
	                ProcessAndAddItem(entry, nullptr, temporarySortedVector);
	            }
	        }
	    }
	
	    // 2. Perform Case-Insensitive Alphabetical Sorting across the entire combined list
	    std::sort(temporarySortedVector.begin(), temporarySortedVector.end(), CompareDrawerItems);
	
	    // 3. Move perfectly organized pointers into native BList architecture
	    for (size_t i = 0; i < temporarySortedVector.size(); ++i) {
	        fItemsList.AddItem(temporarySortedVector[i]);
	    }
	}


    virtual void Draw(BRect updateRect) {
        // =========================================================================
        // NEW STRUCTURAL HEADER SECTION
        // =========================================================================
        float canvasWidth = Bounds().Width();
        
        // 1. Draw Centered Title Text
        SetFont(be_bold_font);
        SetFontSize(20.0f);
        SetHighColor(rgb_color{220, 225, 235, 255}); // Clean crisp white/silver
        
        BString titleStr("hdesktop");
        float titleWidth = StringWidth(titleStr.String());
        BPoint titlePos((canvasWidth - titleWidth) / 2.0f, 30.0f);
        DrawString(titleStr.String(), titlePos);

        // 2. Define Action Icon Boundary Metrics
        // --- Exit button is now calculated dynamically from the right side ---
        BRect configIconRect(canvasWidth - 62.0f, 45.0f, canvasWidth - 31.0f, 76.0f);
        BRect exitIconRect(canvasWidth - 103.0f, 45.0f, canvasWidth - 72.0f, 76.0f); // 31px wide, placed left of config

        // Power buttons stay safely on the far left side
        BRect shutdownSysRect(30.0f, 45.0f, 110.0f, 76.0f); // Shifted over to start at X=30
        BRect rebootSysRect(120.0f, 45.0f, 215.0f, 76.0f);



        // Fetch active cursor coordinates for interface tracking
        BPoint cursorPoint;
        uint32 transitButtons;
        GetMouse(&cursorPoint, &transitButtons, false);
        SetDrawingMode(B_OP_ALPHA);


        
        // --- ADDED: DRAW SHUTDOWN BUTTON ---
        if (shutdownSysRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{220, 60, 60, 45}); // Soft red hover glow
            FillRect(shutdownSysRect);
            SetHighColor(rgb_color{255, 90, 90, 255}); 
        } else {
            SetHighColor(rgb_color{35, 36, 42, 255}); // Dark matte base
            FillRect(shutdownSysRect);
            SetHighColor(rgb_color{210, 100, 100, 255}); 
        }
        StrokeRect(shutdownSysRect);
        
        SetFont(be_plain_font);
        SetFontSize(11.0f);
        BString shutText("Power off");
        float shutTextW = StringWidth(shutText.String());
        DrawString(shutText.String(), BPoint(shutdownSysRect.left + (shutdownSysRect.Width() - shutTextW) / 2.0f, 64.0f));

        // --- ADDED: DRAW REBOOT BUTTON ---
        if (rebootSysRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{60, 140, 220, 45}); // Soft blue hover glow
            FillRect(rebootSysRect);
            SetHighColor(rgb_color{90, 175, 255, 255}); 
        } else {
            SetHighColor(rgb_color{35, 36, 42, 255}); 
            FillRect(rebootSysRect);
            SetHighColor(rgb_color{100, 160, 220, 255}); 
        }
        StrokeRect(rebootSysRect);

        BString rebootText("Restart system");
        float rebootTextW = StringWidth(rebootText.String());
        DrawString(rebootText.String(), BPoint(rebootSysRect.left + (rebootSysRect.Width() - rebootTextW) / 2.0f, 64.0f));

		/*
        // Draw Config Gear Button (Right Side)
        if (configIconRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{100, 120, 160, 50}); // Slate hover glow
            FillRect(configIconRect.InsetBySelf(-4, -4));
            SetHighColor(rgb_color{150, 180, 230, 255});
        } else {
            SetHighColor(rgb_color{130, 140, 160, 200}); // Muted slate vector
        }
        StrokeRect(configIconRect);
        StrokeEllipse(configIconRect.InsetByCopy(6, 6));
      
        // Draw Exit Button (Right Side)
        if (exitIconRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{230, 75, 75, 45}); // Soft red hover glow
            FillRect(exitIconRect.InsetBySelf(-4, -4));
            SetHighColor(rgb_color{255, 90, 90, 255});
        } else {
            SetHighColor(rgb_color{200, 70, 70, 220}); // Muted red vector
        }
        StrokeLine(BPoint(exitIconRect.left, exitIconRect.top), BPoint(exitIconRect.right, exitIconRect.bottom));
        StrokeLine(BPoint(exitIconRect.left, exitIconRect.bottom), BPoint(exitIconRect.right, exitIconRect.top));
	     */

        // 5. Draw Separator Line Accent
        SetHighColor(rgb_color{50, 52, 60, 255}); // Sleek dark baseline border
        StrokeLine(BPoint(20.0f, 90.0f), BPoint(canvasWidth - 20.0f, 90.0f));
        SetHighColor(rgb_color{120, 130, 150, 20}); // Soft ambient highlight
        StrokeLine(BPoint(20.0f, 91.0f), BPoint(canvasWidth - 20.0f, 91.0f));

        // =========================================================================
        // ADJUSTED MULTI-SECTION APPLICATION GRID PIPELINE
        // =========================================================================
        float itemW = 100.0f;
        float itemH = 110.0f;
        float startX = 30.0f;
        float currentY = 115.0f; // Track active height positions dynamically
        float spacingX = 24.0f;
        float spacingY = 20.0f;

        int32 cols = static_cast<int32>((Bounds().Width() - (startX * 2.0f)) / (itemW + spacingX));
        if (cols < 1) cols = 1;

        // Reset tracking modes for icon card hover metrics
        SetFont(be_plain_font);
        SetFontSize(11.0f);

        // 1. Separate your source lists dynamically
        std::vector<DrawerItem*> favoriteItems;
        std::vector<DrawerItem*> standardItems;

        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            DrawerItem* item = (DrawerItem*)fItemsList.ItemAt(i);
            BPath itemPath(&item->ref);
            std::string pathKey(itemPath.Path());

            if (gFavoritePaths.find(pathKey) != gFavoritePaths.end()) {
                favoriteItems.push_back(item);
            } else {
                standardItems.push_back(item);
            }
        }

        // =========================================================================
        // PASS A: RENDER PINNED FAVORITES GRID
        // =========================================================================
        if (!favoriteItems.empty()) {
            SetFont(be_bold_font);
            SetFontSize(12.0f);
            SetHighColor(rgb_color{130, 145, 180, 200}); // Dim slate text
            DrawString("Favorites", BPoint(startX, currentY + 10.0f));
            currentY += 25.0f;

            SetFont(be_plain_font);
            SetFontSize(11.0f);

            for (size_t i = 0; i < favoriteItems.size(); i++) {
                int32 c = i % cols;
                int32 r = i / cols;

                float x = startX + (c * (itemW + spacingX));
                float y = currentY + (r * (itemH + spacingY));
                BRect itemBounds(x, y, x + itemW, y + itemH);

                // Highlight Hover
                if (itemBounds.Contains(cursorPoint)) {
                    SetDrawingMode(B_OP_ALPHA);
                    SetHighColor(rgb_color{100, 140, 220, 30}); // Soft blue highlight glow
                    FillRect(itemBounds);
                    SetHighColor(rgb_color{130, 160, 220, 70});
                    StrokeRect(itemBounds);
                }

                DrawerItem* item = favoriteItems[i];
                if (item->icon) {
                    SetDrawingMode(B_OP_ALPHA);
                    DrawBitmap(item->icon, BPoint(x + (itemW / 2.0f) - 24.0f, y + 15.0f));
                }

                SetHighColor(rgb_color{240, 240, 245, 255});
                BString truncatedName = item->name;
                TruncateString(&truncatedName, B_TRUNCATE_END, itemW - 10.0f);
                float textW = StringWidth(truncatedName.String());
                DrawString(truncatedName.String(), BPoint(x + (itemW / 2.0f) - (textW / 2.0f), y + 90.0f));
            }

            int32 favRows = (favoriteItems.size() + cols - 1) / cols;
            currentY += (favRows * (itemH + spacingY)) + 15.0f;

            // Draw clean secondary dividing line separating lists
            SetHighColor(rgb_color{50, 52, 60, 120}); 
            StrokeLine(BPoint(startX, currentY - 5.0f), BPoint(canvasWidth - startX, currentY - 5.0f));
        }

        // =========================================================================
        // PASS B: RENDER REMAINING STANDARD APPLICATIONS GRID
        // =========================================================================
        SetFont(be_bold_font);
        SetFontSize(12.0f);
        SetHighColor(rgb_color{120, 125, 135, 180});
        DrawString("Applications", BPoint(startX, currentY + 10.0f));
        currentY += 25.0f;

        SetFont(be_plain_font);
        SetFontSize(11.0f);

        for (size_t i = 0; i < standardItems.size(); i++) {
            int32 c = i % cols;
            int32 r = i / cols;

            float x = startX + (c * (itemW + spacingX));
            float y = currentY + (r * (itemH + spacingY));
            BRect itemBounds(x, y, x + itemW, y + itemH);

            if (itemBounds.Contains(cursorPoint)) {
                SetDrawingMode(B_OP_ALPHA);
                SetHighColor(rgb_color{100, 110, 140, 30}); 
                FillRect(itemBounds);
                SetHighColor(rgb_color{130, 145, 180, 70});
                StrokeRect(itemBounds);
            }

            DrawerItem* item = standardItems[i];
            if (item->icon) {
                SetDrawingMode(B_OP_ALPHA);
                DrawBitmap(item->icon, BPoint(x + (itemW / 2.0f) - 24.0f, y + 15.0f));
            }

            SetHighColor(rgb_color{240, 240, 245, 255});
            BString truncatedName = item->name;
            TruncateString(&truncatedName, B_TRUNCATE_END, itemW - 10.0f);
            float textW = StringWidth(truncatedName.String());
            DrawString(truncatedName.String(), BPoint(x + (itemW / 2.0f) - (textW / 2.0f), y + 90.0f));
        }

        // Recalculate virtual container height correctly to scale scrolling properties
        int32 stdRows = (standardItems.size() + cols - 1) / cols;
        float targetVirtualHeight = currentY + (stdRows * (itemH + spacingY)) + 40.0f;
        
        if (Bounds().Height() != targetVirtualHeight) {
            ResizeTo(Bounds().Width(), targetVirtualHeight);
        }

    }
    

		virtual void MessageReceived(BMessage* message) {
		    switch (message->what) {
		    	
		        case 'tfav': {
		            const char* pathStr = nullptr;
		            if (message->FindString("path", &pathStr) == B_OK && pathStr != nullptr) {
		                std::string targetKey(pathStr);
		                
		                std::set<std::string>::iterator it = gFavoritePaths.find(targetKey);
		                if (it != gFavoritePaths.end()) {
		                    gFavoritePaths.erase(it);
		                } else {
		                    gFavoritePaths.insert(targetKey);
		                }
		
		                // Write the message flattening block safely back down to disk
		                SaveConfiguration();
		                
		                // Signal an internal redrawing pass 
		                Invalidate();
		            }
		            break;
		        }

		    	
		        case B_MOUSE_WHEEL_CHANGED: {
		            float deltaY = 0.0f;
		            
		            // Extract the vertical wheel movement delta
		            if (message->FindFloat("be:wheel_delta_y", &deltaY) == B_OK && deltaY != 0.0f) {
		                // Multiplier to increase scrolling speed (adjust 30.0f to taste)
		                float scrollSpeedMultiplier = 30.0f; 
		                float scrollAmount = deltaY * scrollSpeedMultiplier;
		
		                // Option A: If your view is hosted directly in a standard BScrollView
		                if (ScrollBar(B_VERTICAL)) {
		                    float currentVal = ScrollBar(B_VERTICAL)->Value();
		                    ScrollBar(B_VERTICAL)->SetValue(currentVal + scrollAmount);
		                } 
		                // Option B: If your view handles its own internal drawing offset bounds
		                else {
		                    ScrollBy(0.0f, scrollAmount);
		                }
		            }
		            break;
		        }
		        default:
		            // Pass unhandled messages up to the base class
		            BView::MessageReceived(message);
		            break;
		    }
		}

    virtual void MouseDown(BPoint point) {
        float canvasWidth = Bounds().Width();
        
        // Define Action Header Click Target Boundaries
        // --- Coordinates mirrored to match Draw view layout modifications ---
        BRect configIconRect(canvasWidth - 62.0f, 45.0f, canvasWidth - 31.0f, 76.0f);
        BRect exitIconRect(canvasWidth - 103.0f, 45.0f, canvasWidth - 72.0f, 76.0f);

        BRect shutdownSysRect(30.0f, 45.0f, 110.0f, 76.0f);
        BRect rebootSysRect(120.0f, 45.0f, 190.0f, 76.0f);

		
        // --- ADDED: INTERCEPT SHUTDOWN SYSTEM TRACKER ---
        if (shutdownSysRect.Contains(point)) {

            std::system("shutdown &");
            if (Window()) {
                Window()->Quit();
            }

            return;
        }

        // --- ADDED: INTERCEPT REBOOT SYSTEM TRACKER ---
        if (rebootSysRect.Contains(point)) {
            std::system("shutdown -r &");
            if (Window()) {
                Window()->Quit();
            }

            return;
        }
		
		
		/*
	    // 1. Process Exit Button Trigger Click
        if (exitIconRect.Contains(point)) {

            if (Window()) {
                Window()->Quit(); 
            }
            return;
        }
        // 2. Process Configuration Button Trigger Click
        if (configIconRect.Contains(point)) {           
            if (gActiveConfigInstance == nullptr && Window()) {
                // Instantiate the sub-panel, centering it smoothly within the app drawer's viewport bounds
                gActiveConfigInstance = new HaikuConfigWindow(Window()->Frame());
                gActiveConfigInstance->Show();
            } else if (gActiveConfigInstance != nullptr) {
                // If the panel is already open, pull it to the front instead of duplicating it
                gActiveConfigInstance->Activate(true);
            }
            return;
        }
		*/

        // =========================================================================
        // 3. Process Standard Grid Icon Coordinates
        // =========================================================================
        float itemW = 100.0f;
        float itemH = 110.0f;
        float startX = 30.0f;
        float currentY = 115.0f; 
        float spacingX = 24.0f;
        float spacingY = 20.0f;

        int32 cols = static_cast<int32>((Bounds().Width() - (startX * 2.0f)) / (itemW + spacingX));
        if (cols < 1) cols = 1;

        uint32 buttons = 0;
        if (Window() && Window()->CurrentMessage()) {
            Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
        }

        // Split tracking groups to match Draw locations identically
        std::vector<DrawerItem*> favoriteItems;
        std::vector<DrawerItem*> standardItems;

        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            DrawerItem* item = (DrawerItem*)fItemsList.ItemAt(i);
            BPath itemPath(&item->ref);
            std::string pathKey(itemPath.Path());

            if (gFavoritePaths.find(pathKey) != gFavoritePaths.end()) {
                favoriteItems.push_back(item);
            } else {
                standardItems.push_back(item);
            }
        }

        // --- CHECK TARGETS A: EVAPORATE FAVORITES SECTOR CLICK ACTION ---
        if (!favoriteItems.empty()) {
            currentY += 25.0f; // Header Text Padding
            for (size_t i = 0; i < favoriteItems.size(); i++) {
                int32 c = i % cols;
                int32 r = i / cols;

                float x = startX + (c * (itemW + spacingX));
                float y = currentY + (r * (itemH + spacingY));
                BRect itemBounds(x, y, x + itemW, y + itemH);

                if (itemBounds.Contains(point)) {
                    DrawerItem* item = favoriteItems[i];
                    BPath itemPath(&item->ref);
                    std::string pathKey(itemPath.Path());

                    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                        BPopUpMenu* contextMenu = new BPopUpMenu("Context", false, false);
                        BMessage* toggleMsg = new BMessage('tfav');
                        toggleMsg->AddString("path", pathKey.c_str());
                        contextMenu->AddItem(new BMenuItem("Remove Favorite", toggleMsg));
                        contextMenu->SetTargetForItems(this);
                        contextMenu->Go(ConvertToScreen(point), true, true, true);
                        return;
                    }
                    if (buttons & B_PRIMARY_MOUSE_BUTTON) {
                        be_roster->Launch(&item->ref);
                        if (Window()) Window()->Quit();
                        return;
                    }
                }
            }
            int32 favRows = (favoriteItems.size() + cols - 1) / cols;
            currentY += (favRows * (itemH + spacingY)) + 15.0f;
        }

        // --- CHECK TARGETS B: EVAPORATE STANDARD SECTOR CLICK ACTION ---
        currentY += 25.0f; // Header Text Padding
        for (size_t i = 0; i < standardItems.size(); i++) {
            int32 c = i % cols;
            int32 r = i / cols;

            float x = startX + (c * (itemW + spacingX));
            float y = currentY + (r * (itemH + spacingY));
            BRect itemBounds(x, y, x + itemW, y + itemH);

            if (itemBounds.Contains(point)) {
                DrawerItem* item = standardItems[i];
                BPath itemPath(&item->ref);
                std::string pathKey(itemPath.Path());

                if (buttons & B_SECONDARY_MOUSE_BUTTON) {
                    BPopUpMenu* contextMenu = new BPopUpMenu("Context", false, false); 
                    BMessage* toggleMsg = new BMessage('tfav');
                    toggleMsg->AddString("path", pathKey.c_str());
                    contextMenu->AddItem(new BMenuItem("Add Favorite", toggleMsg));
                    contextMenu->SetTargetForItems(this);
                    contextMenu->Go(ConvertToScreen(point), true, true, true);
                    return;
                }

                if (buttons & B_PRIMARY_MOUSE_BUTTON) {
                    be_roster->Launch(&item->ref);
                    if (Window()) Window()->Quit();
                    return;
                }
            }
        }
    }
};

// =========================================================================
// NATIVE BWINDOW BORDERLESS COMPONENT LAYOUT (DYNAMIC WIDTH MATRIX MANAGER)
// =========================================================================
class HaikuAppDrawerWindow : public BWindow {
private:
    BMessageRunner* fHoverTicker;
    bool            fMouseHasEntered; // Safety check flag

public:
    HaikuAppDrawerWindow(float screenH)
        : BWindow(BRect(0, 0, 100, 100), "App Dashboard Menu",
                  B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, 
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE),
          fHoverTicker(nullptr),
          fMouseHasEntered(false) { // Initially false so it won't instant-close
        
        BScreen activeScreen(this);
        BRect screenFrame = activeScreen.Frame();
        
        float horizontalMarginGap = 40.0f;
        float targetPanelWidth = screenFrame.Width() - (horizontalMarginGap * 2.0f);
        float targetPanelHeight = screenH - 200.0f;

        MoveTo(horizontalMarginGap, 30.0f);
        ResizeTo(targetPanelWidth, targetPanelHeight);

        BRect bounds = Bounds();
        bounds.right -= B_V_SCROLL_BAR_WIDTH; 

        DrawerView* drawerView = new DrawerView(bounds);
        BScrollView* scrollView = new BScrollView("DashboardScroll", drawerView, 
            B_FOLLOW_ALL, 0, false, true, B_NO_BORDER);
            
        AddChild(scrollView);

        // Run the position checker 10 times a second
        BMessage tickMessage('tick');
        fHoverTicker = new BMessageRunner(BMessenger(this), &tickMessage, 100000);
    }
    
    virtual ~HaikuAppDrawerWindow() {
        delete fHoverTicker;
        gActiveDrawerInstance = nullptr;
    }

    virtual void MessageReceived(BMessage* message) {
        switch (message->what) {
            case 'tick': {
                if (IsHidden()) return;

                BPoint screenMousePos;
                uint32 buttons;
                
                if (ChildAt(0)) {
                    ChildAt(0)->GetMouse(&screenMousePos, &buttons, false);
                    ChildAt(0)->ConvertToScreen(&screenMousePos);

                    bool isInsideFrame = Frame().Contains(screenMousePos);

                    if (!fMouseHasEntered && isInsideFrame) {
                        fMouseHasEntered = true;
                    }

                    if (!isInsideFrame) {
                        BScreen screen(this);
                        BRect screenFrame = screen.Frame();
                        
                        // Protects the dock area from triggering an auto-close
                        if (screenMousePos.y >= (screenFrame.bottom - 100.0f)) {
                            break; 
                        }

                        if (fMouseHasEntered) {
                            PostMessage(B_QUIT_REQUESTED);
                        }
                    }
                }
                break;
            }
            default:
                BWindow::MessageReceived(message);
                break;
        }
    }


};








BString GetActiveHaikuWallpaperPath() {
    BString targetWallpaperPath = "";
    BPath desktopPath;

    // 1. Get the current active workspace index (0-indexed) and convert to a bitmask
    int32 currentWorkspaceIndex = current_workspace();
    uint32 currentWorkspaceMask = 1 << currentWorkspaceIndex;

    // 2. Resolve and attach to the Desktop directory node
    if (find_directory(B_DESKTOP_DIRECTORY, &desktopPath) == B_OK) {
        BNode desktopNode(desktopPath.Path());
        attr_info info;

        if (desktopNode.InitCheck() == B_OK && 
            desktopNode.GetAttrInfo("be:bgndimginfo", &info) == B_OK && info.size > 0) {
            
            char* buffer = new(std::nothrow) char[info.size];
            if (buffer != nullptr) {
                if (desktopNode.ReadAttr("be:bgndimginfo", info.type, 0, buffer, info.size) == info.size) {
                    BMessage container;
                    if (container.Unflatten(buffer) == B_OK) {
                        
                        BString fallbackPath = "";

                        // 3. Loop through the array to find the match for our current workspace
                        for (int32 index = 0; ; index++) {
                            int32 workspaceMask = 0;
                            
                            // Check if we reached the end of the data matrix
                            if (container.FindInt32("be:bgndimginfoworkspaces", index, &workspaceMask) != B_OK) {
                                break;
                            }

                            const char* extractedPath = nullptr;
                            if (container.FindString("be:bgndimginfopath", index, &extractedPath) == B_OK) {
                                if (extractedPath != nullptr && extractedPath[0] != '\0') {
                                    
                                    BEntry imageFile(extractedPath);
                                    if (imageFile.Exists() && !imageFile.IsDirectory()) {
                                        
                                        // If this entry explicitly targets our current workspace bit, choose it immediately
                                        if ((workspaceMask & currentWorkspaceMask) != 0) {
                                            targetWallpaperPath = extractedPath;
                                            break; 
                                        }

                                        // Fallback if no specific workspace match is found later
                                        if (fallbackPath.IsEmpty()) {
                                            fallbackPath = extractedPath;
                                        }
                                    }
                                }
                            }
                        }

                        // If no specific workspace rule matched, fall back to the first valid image found
                        if (targetWallpaperPath.IsEmpty()) {
                            targetWallpaperPath = fallbackPath;
                        }
                    }
                }
                delete[] buffer;
            }
        }
    }

    return targetWallpaperPath;
}


// =========================================================================
// THE HAIKU DESKTOP DRAW ENGINE RENDERING INTERFACE CLASS
// =========================================================================
class HaikuGlDesktopEngine {
private:
	bool fShouldDrawList = false;
	team_id fHoveredTeam = -1;
	std::vector<TrackedWindowInfo> fCurrentWindowsList;

public:
    HaikuGlDesktopEngine(int width, int height) : fWidth(width), fHeight(height) {

        fBgColorR = 0.20f; fBgColorG = 0.42f; fBgColorB = 0.58f;         
        fMouseX = 0;
        fMouseY = 0;
        
        fTrashTooltipTexId = 0; 
		fTrashTextGenerated = false;
		fTrashTooltipW = 0;
		fTrashTooltipH = 0;
		fLastTrackerMenuCloseTime = 0;

        fHaikuMenuIcon = LoadIconFromNode("/boot/system/apps/AboutSystem", 128);
        fHaikuTrashIcon = LoadIconFromNode("/boot/trash", 128);

        BString capturedWallpaper = GetActiveHaikuWallpaperPath();
        fWallpaperTexture = LoadWallpaperViaTranslationKit(capturedWallpaper.String());

        // =========================================================================
        // INITIALIZE SYSTEM MONITOR 
        // =========================================================================

        fLastCpuPulseTime = SDL_GetTicks();
        for (int i = 0; i < 40; ++i) {
            fCpuHistory[i] = 0.0f;
        }
        system_info info;
        if (get_system_info(&info) == B_OK) {
            fPrevTotalTicks = 0;
            fPrevActiveTicks = 0;
            cpu_info* cpuInfos = new cpu_info[info.cpu_count];
            if (get_cpu_info(0, info.cpu_count, cpuInfos) == B_OK) {
                for (uint32_t c = 0; c < info.cpu_count; ++c) {
                    fPrevActiveTicks += cpuInfos[c].active_time;
                }
            }
            delete[] cpuInfos;
        }


    } // End of complete class constructor



	void ReloadWallpaperBackground() {
	    // Use the inner integer identifier field for the OpenGL cleanup
	    if (fWallpaperTexture.id != 0) {
	        glDeleteTextures(1, &fWallpaperTexture.id);
	        fWallpaperTexture.id = 0;
	    }
	
	    BString capturedWallpaper = GetActiveHaikuWallpaperPath();
	    fWallpaperTexture = LoadWallpaperViaTranslationKit(capturedWallpaper.String());
	}


    void HandleMouseWheel(int wheelStepY) {
        // 1. Check if the mouse cursor is physically hovering over the volume bar right now
        bool isMouseOverSlider = (fMouseX >= fCachedVolLeft && fMouseX <= (fCachedVolLeft + fCachedVolWidth) &&
                                  fMouseY >= fCachedVolTop && fMouseY <= (fCachedVolTop + fCachedVolHeight));

        if (isMouseOverSlider) {
            // Adjust the system volume level cleanly by 2% increments per notch tick step
            float volumeStepIncrement = 0.02f;
            fCurrentVolumeLevel += static_cast<float>(wheelStepY) * volumeStepIncrement;
            
            // Hard clamp boundaries to protect the system mixer gain tables
            if (fCurrentVolumeLevel < 0.0f) fCurrentVolumeLevel = 0.0f;
            if (fCurrentVolumeLevel > 1.0f) fCurrentVolumeLevel = 1.0f;
            
            // Write updates directly down to Haiku's underlying media kit hardware nodes!
            SetHaikuMixerVolume(fCurrentVolumeLevel);
            return; // Skip desktop canvas scrolling adjustments completely
        }

        // =========================================================================
        // FALLBACK: ORIGINAL DESKTOP SHORTCUT WINDOW CANVAS SCROLLING LOGIC
        // =========================================================================
        float scrollSpeed = 30.0f;
        fScrollOffset -= static_cast<float>(wheelStepY) * scrollSpeed;
        
        if (fScrollOffset < 0.0f) fScrollOffset = 0.0f;
        if (fScrollOffset > fMaxScrollOffset) fScrollOffset = fMaxScrollOffset;
        
    }


	void SyncDockWithRunningDeskbarApps() {
	    // --- CRITICAL ORIGINAL LEAK RECLAIM: FREE EXISTING TRACKING ICONS ---
	    // =================================================================
	    for (size_t i = 0; i < fTaskbarWindows.size(); i++) {
	        // Look inside your existing TaskbarItem layout parameters
	        if (fTaskbarWindows[i].icon.id > 0) {
	            // Force OpenGL to instantly liberate the graphic texture memory allocations
	            glDeleteTextures(1, &fTaskbarWindows[i].icon.id);
	            fTaskbarWindows[i].icon.id = 0; // Reset flag to guarantee safety
	        }
	    }
	    
	    // 1. Keep a local backup so we don't blow away our click modifications
	    std::vector<TaskbarItem> oldTaskbarWindows = fTaskbarWindows;
	    fTaskbarWindows.clear();
	
	    std::vector<std::string> processedSignatures;
	
	    // Fetch the absolute active app info once up front to optimize the loop
	    app_info activeAppInfo;
	    team_id activeTeamId = -1;
	    if (be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
	        activeTeamId = activeAppInfo.team;
	    }
	
	    // 2. Query the global Haiku roster for all active running teams
	    BList teamList;
	    be_roster->GetAppList(&teamList);
	
	    int32 count = teamList.CountItems();
	    for (int32 i = 0; i < count; ++i) {
	        team_id id = (team_id)(addr_t)teamList.ItemAt(i);
	        
	        app_info info;
	        // Hide background apps and/or other apps we don't want to see.
	        if (be_roster->GetRunningAppInfo(id, &info) == B_OK) {
	            if ((info.flags & B_BACKGROUND_APP) != 0) continue;
	            if (strcmp(info.signature, "application/x-vnd.Be-SYS.SleepWalker") == 0) continue;
	
	            std::string appSignature(info.signature);
	            
	            // --- Duplicate Filter For Firefox and Other Clones ---
	            // =================================================================
	            bool isDuplicate = false;
	            if (appSignature == "application/x-vnd.iceweasel"   	|| 
	            	appSignature == "application/x-vnd.Mozilla-Firefox" || 
	            	appSignature == "application/x-vnd.waterfox" 		|| 
	            	appSignature == "application/x-vnd.floorp-browser") {
	                for (const auto& sig : processedSignatures) {
	                    if (sig == appSignature) {
	                        isDuplicate = true; 
	                        break;
	                    }
	                }
	            }
	            if (isDuplicate) continue; // Skip subsequent Iceweasel/Firefox teams, allow all other apps
	
	            BEntry entry(&info.ref);
	            if (entry.InitCheck() != B_OK) continue;
	
	            char nameBuf[B_FILE_NAME_LENGTH];
	            entry.GetName(nameBuf);
	            std::string appTitle(nameBuf);
	
	            if (appTitle == "Deskbar") {
	                continue;
	            }
	
	            BPath path;
	            entry.GetPath(&path);
	
	            TaskbarItem openApp;
	            openApp.title = appTitle;
	            openApp.icon = LoadIconFromNode(path.Path(), 128); 
	            openApp.teamId = id; 
	
	            // --- FIXED STATE RESTORE LAYER WITH FOREGROUND CHECK ---
	            bool foundOldInstance = false;
	            for (const auto& oldWin : oldTaskbarWindows) {
	                if (oldWin.teamId == id) {
	                    openApp.isMinimized = oldWin.isMinimized;
	                    foundOldInstance = true;
	                    break;
	                }
	            }
	
	            // CORRECTION: If it's a completely new application node or our dock app itself 
	            // currently holds stolen click focus, evaluate it cleanly via the roster information.
	            if (!foundOldInstance || activeTeamId == id) {
	                // If it's the absolute front window, it is not minimized
	                if (activeTeamId == id) {
	                    openApp.isMinimized = false;
	                } 
	                // If our dock app currently holds focus, fallback safely to its previous state 
	                // or assume it's minimized if it wasn't tracked yet and isn't us
	                else if (activeTeamId == be_app->Team()) {
	                    openApp.isMinimized = foundOldInstance ? openApp.isMinimized : true;
	                } 
	                else {
	                    openApp.isMinimized = true;
	                }
	            }
	
	            static bool sAlwaysTrue = true;
	            openApp.openStateFlag = &sAlwaysTrue;
	            openApp.minimizeStateFlag = &openApp.isMinimized; 
	
	            // Store signatures to maintain historical state tracking for the filtered targets
	            processedSignatures.push_back(appSignature);
	            fTaskbarWindows.push_back(openApp);
	        }
	    }
	}



	struct TrackerMenuArgs {
	    HaikuGlDesktopEngine* engine;
	    int32 winX;
	    int32 winY;
	    int32 mouseX;
	    int32 mouseY; 
	    float currentDockH;
	};
	
	// 2. UPDATED BACKGROUND THREAD FUNCTION
	static int32 SpawnTrackerMenuThread(void* cookie) {
	    TrackerMenuArgs* args = static_cast<TrackerMenuArgs*>(cookie);
	    
	    BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	
	    entry_ref rootRef;
	    BEntry rootEntry("/boot/");
	    if (rootEntry.GetRef(&rootRef) != B_OK) {
	        args->engine->fTrackerMenuIsActive = false; // Release safety latch
	        delete args;
	        return B_ERROR;
	    }
	
	    BPopUpMenu* navMenuWrapper = new BPopUpMenu("", false, false);
	    navMenuWrapper->SetRadioMode(false);
	
	    BPrivate::BNavMenu* asyncNavMenu = new BPrivate::BNavMenu("TempNav", B_REFS_RECEIVED, trackerMessenger);
	    asyncNavMenu->SetNavDir(&rootRef);
	
	    asyncNavMenu->AttachedToWindow();
	    snooze(10000); 
	
	    int32 totalNavItems = asyncNavMenu->CountItems();
	    if (totalNavItems > 0) {
	        for (int32 idx = 0; idx < totalNavItems; ++idx) {
	            BMenuItem* extractedItem = asyncNavMenu->RemoveItem(static_cast<int32>(0));
	            if (extractedItem) {
	                navMenuWrapper->AddItem(extractedItem);
	            }
	        }
	    } else {
	        navMenuWrapper->AddItem(new BMenuItem("Open Tracker /boot/", new BMessage(B_REFS_RECEIVED)));
	    }
	
	    float anchoredMenuX = static_cast<float>(args->winX + args->mouseX) - 45.0f;
	    if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f;
	    
	    // SMART ADJUSTMENT: Calculate layout normalization metrics boundary limit tracker
	    float maxExpectedHeight = 164.0f; 
	    float structuralOffset = maxExpectedHeight - args->currentDockH;
	    if (structuralOffset < 0.0f) structuralOffset = 0.0f; // Safety clamp to prevent clipping
	    
	    // FIX: Push it lower down the screen boundary context as your dock container shrivels
	    float anchoredMenuY = static_cast<float>(args->winY) + structuralOffset - 5.0f;
	    BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);
	
	    // BLOCKING CALL (Inside background thread only): Freezes safely until user chooses or clicks away
	    BMenuItem* chosenAction = navMenuWrapper->Go(screenClickPoint, false, false);
	
	    // =========================================================================
	    // RESTORED SHIELD LOGIC: LOG TIMESTAMPS ONLY ON ACTUAL MENU COLLAPSE
	    // =========================================================================
	    args->engine->fLastTrackerMenuCloseTime = SDL_GetTicks();
	    args->engine->fTrackerMenuIsActive = false; // Reset the active status lock flag
	
	    if (chosenAction != nullptr) {
	        BMessage* actionMsg = chosenAction->Message();
	        if (actionMsg != nullptr && actionMsg->what == B_REFS_RECEIVED) {
	            if (trackerMessenger.IsValid()) {
	                trackerMessenger.SendMessage(actionMsg);
	            }
	        }
	    }
	
	    delete asyncNavMenu;
	    delete navMenuWrapper;
	    delete args; 
	    return B_OK;
	}


   
	void HandleMouseClick(int x, int y, int button) {
		
	    // Sync global mouse variables to match click coordinates
	    fMouseX = x; 
	    fMouseY = y;
	
	    // =========================================================================
	    // DYNAMIC SYSTEM TRAY INTERCEPTOR & SERIALIZED PROPERTY INSPECTOR (NON-BLOCKING)
	    // =========================================================================
	    if (showSystemTray) {
		    for (const auto& item : fLiveTrayItems) {
		        if (item.currentRenderWidth <= 0.0f) continue;
		
		        if (x >= item.currentRenderX && x <= (item.currentRenderX + item.currentRenderWidth) &&
		   		    y >= fCachedVolTop && y <= (fCachedVolTop + fCachedVolHeight)) {
		
		            BMessenger deskbarMessenger("application/x-vnd.be-tskb");
		            if (deskbarMessenger.IsValid()) {
		                
		                // =========================================================================
		                // CASE A: RIGHT-CLICK -> ASYNCHRONOUS DESKBAR REPLICANT MENU RESOLVER
		                // =========================================================================
		                if (button == SDL_BUTTON_RIGHT) {
                            uint32 currentClickTick = SDL_GetTicks();
                            
                            // Reused sequential toggle shield
                            if (currentClickTick - fLastTrackerMenuCloseTime < 150) {
                                fLastTrackerMenuCloseTime = 0; 
                                return; 
                            }

                            // Reused active menu latch check
                            if (fTrackerMenuIsActive) {
                                return; 
                            }

                            fTrackerMenuIsActive = true; 

                            int winX = 0, winY = 0;
                            SDL_Window* activeWin = SDL_GetMouseFocus();
                            if (activeWin) {
                                SDL_GetWindowPosition(activeWin, &winX, &winY);
                            }

                            // Package specialized isolated heap variables for the background thread context
                            SystrayMenuArgs* args = new SystrayMenuArgs();
                            args->engine = this; 
                            args->winX = winX;
                            args->winY = winY;
                            args->mouseX = x; 
                            args->mouseY = y; 
                            args->itemName = item.name; // Deep string copy guarantees memory protection

                            // SELF-CONTAINED INLINE THREAD POINTER
                            int32 (*inlineSystrayFunc)(void*) = [](void* data) -> int32 {
                                SystrayMenuArgs* threadArgs = static_cast<SystrayMenuArgs*>(data);
                                if (!threadArgs || !threadArgs->engine) {
                                    delete threadArgs;
                                    return B_ERROR;
                                }

                                BMessenger deskbarMessenger("application/x-vnd.be-tskb");
                                if (deskbarMessenger.IsValid()) {
                                    
                                    BMessage menuRequest(B_GET_PROPERTY);
                                    menuRequest.AddSpecifier("Menu");
                                    menuRequest.AddSpecifier("Replicant", threadArgs->itemName.c_str());
                                    menuRequest.AddSpecifier("View", "Status");
                                    menuRequest.AddSpecifier("View", "Deskbar");

                                    BMessage menuReply;
                                    // Synchronous query happens entirely inside this worker thread context!
                                    if (deskbarMessenger.SendMessage(&menuRequest, &menuReply) == B_OK) {
                                        
                                        BPopUpMenu* localMenu = new BPopUpMenu("SystrayContext", false, false);
                                        localMenu->SetRadioMode(false);
                                        
                                        BMessage archivedItem;
                                        int32 itemIdx = 0;
                                        bool foundItems = false;

                                        while (menuReply.FindMessage("item", itemIdx, &archivedItem) == B_OK || 
                                               menuReply.FindMessage("_items", itemIdx, &archivedItem) == B_OK) {
                                            
                                            const char* label = nullptr;
                                            if (archivedItem.FindString("label", &label) == B_OK && label != nullptr) {
                                                BMessage* forwardMsg = new BMessage(archivedItem);
                                                localMenu->AddItem(new BMenuItem(label, forwardMsg));
                                                foundItems = true;
                                            }
                                            archivedItem.MakeEmpty();
                                            itemIdx++;
                                        }

                                        if (!foundItems) {
                                            if (threadArgs->itemName == "ProcessController" || threadArgs->itemName == "ProcessControllerView") {
                                                localMenu->AddItem(new BMenuItem("Memory Usage Profiles...", new BMessage('act2')));
                                            } else if (threadArgs->itemName == "NetworkStatus") {
                                                localMenu->AddItem(new BMenuItem("Open Network Preferences...", new BMessage('net1')));
                                            } else if (threadArgs->itemName == "MediaReplicant") {
                                                localMenu->AddItem(new BMenuItem("Open Audio Mixer Preferences...", new BMessage('aud1')));
                                            }
                                        }

                                        // Aligns popup horizontally, then places it right above the dock frame
                                        float anchoredMenuX = static_cast<float>(threadArgs->winX + threadArgs->mouseX) - 45.0f;
                                        if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f;
                                        
                                        // TWEAKED: Changed from -5.0f to +20.0f to slide the menu downwards
                                        float anchoredMenuY = static_cast<float>(threadArgs->winY) + 10.0f; 
                                        BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);

                                        BMenuItem* chosenItem = localMenu->Go(screenClickPoint, false, false);

                                        
                                        if (chosenItem != nullptr) {
                                            BMessage* choiceAction = chosenItem->Message();
                                            if (choiceAction != nullptr) {
                                                if (choiceAction->what == 'act2') {
                                                    std::system("/boot/system/apps/ActivityMonitor &");
                                                } else if (choiceAction->what == 'net1') {
                                                    std::system("/boot/system/preferences/Network &");
                                                } else if (choiceAction->what == 'aud1') {
                                                    std::system("/boot/system/preferences/Media &");
                                                } else {
                                                    BMessenger replicantTarget("application/x-vnd.be-tskb");
                                                    replicantTarget.SendMessage(choiceAction);
                                                }
                                            }
                                        }
                                        delete localMenu;
                                    }
                                }

                                // Reset structural security latches safely upon loop completion
                                threadArgs->engine->fLastTrackerMenuCloseTime = SDL_GetTicks();
                                threadArgs->engine->fTrackerMenuIsActive = false;

                                delete threadArgs; 
                                return B_OK;
                            };

                            thread_id menuThread = spawn_thread(inlineSystrayFunc, "async_systray_menu", B_NORMAL_PRIORITY, args);
                            if (menuThread >= B_OK) {
                                resume_thread(menuThread);
                            } else {
                                fTrackerMenuIsActive = false;
                                delete args;
                            }
		                } 
		                // =========================================================================
		                // CASE B: LEFT-CLICK -> RESILIENT PREFERENCE PANEL LAUNCHERS (NON-BLOCKING)
		                // =========================================================================
		                else if (button == SDL_BUTTON_LEFT) {
		                    if (item.name == "MediaReplicant") {
		                        std::system("/boot/system/preferences/Media &");
		                    } else if (item.name == "NetworkStatus") {
		                        std::system("/boot/system/preferences/Network &");
		                    } else if (item.name == "ProcessController" || item.name == "ProcessControllerView") {
		                        //std::system("/boot/system/apps/ProcessController &");
		                    } else if (item.name == "SuperMusicTrayIcon") {
		                        std::system("/boot/system/apps/HaikuSuperMusicThingy &");
		                    }
		                }
		            }
		            return; // Intercept event completely!
		        }
		    }
	    }


	
	    // =========================================================================
	    // CACHED VOLUME SLIDER INTERACTION INTERCEPTOR (SINGLE-CLICK SYNCED)
	    // =========================================================================
	    bool isClickOverSlider = (x >= fCachedVolLeft && x <= (fCachedVolLeft + fCachedVolWidth) &&
	                              y >= fCachedVolTop && y <= (fCachedVolTop + fCachedVolHeight));
	
	    if (isClickOverSlider) {
	        // CASE A: MIDDLE MOUSE BUTTON CENTER-CLICK -> TOGGLE MUTE STATE
	        if (button == SDL_BUTTON_MIDDLE) {
	            if (fCurrentVolumeLevel > 0.0f) {
	                fPreMuteVolumeLevel = fCurrentVolumeLevel;
	                fCurrentVolumeLevel = 0.0f;
	            } else {
	                fCurrentVolumeLevel = (fPreMuteVolumeLevel > 0.0f) ? fPreMuteVolumeLevel : 0.2f;
	            }
	            
	            SetHaikuMixerVolume(fCurrentVolumeLevel);
	            return; // Intercept event completely
	        }
	
	        // =========================================================================
	        // FIX: LEFT MOUSE BUTTON SINGLE-CLICK -> OPEN MEDIA PREFERENCES
	        // Matches the behavior of the Clock and CPU graph seamlessly!
	        // =========================================================================
	        if (button == SDL_BUTTON_LEFT) {
	            // Track mouse down coordinates to distinguish a quick tap from a drag gesture
	            static int initialClickX = -1;
	            
	            // If the user clicked inside the bar but didn't drag back and forth across 
	            // the capsule track (e.g., variance is less than 4 horizontal screen pixels),
	            // we process it as a native single click.
	            if (initialClickX == -1 || std::abs(x - initialClickX) < 4) {
	                std::system("/boot/system/preferences/Media &");
	                initialClickX = -1; // Reset tap latch
	                return; // Intercept event completely, prevent launcher execution bypass below
	            }
	            initialClickX = x;
	        }
	    }
	
        // =========================================================================
        // 3. TASKBAR-ENABLED DOCK WIDTH GEOMETRY CALCULATIONS (UNIFIED ZOOM PIPELINE)
        // =========================================================================
        // FIXED SIZING PIPELINE: Replaced the hardcoded '48.0f' float limits completely 
        // with your live fBaseIconSize configuration setting variable!
        float baseSize = fBaseIconSize;
        float padding  = 16.0f;
        
        size_t baselineLaunchersCount = fDesktopItems.size() + 1; 

        size_t activeWindowsCount = 0;
        for (const auto& w : fTaskbarWindows) {
            if (*w.openStateFlag == true) activeWindowsCount++;
        }

        size_t totalIconsCount = baselineLaunchersCount + activeWindowsCount;

        // Configuration variables for the status widgets (Now scales dynamically relative to icon size changes!)
        float clockSectionPadding = 24.0f;
        float cpuGraphWidth       = 60.0f;
        float separatorGapPadding = 16.0f;
        
        // FIXED SIZING: The trash bin launcher now scales uniformly alongside your application icons
        float baseTrashSize       = fBaseIconSize; 
        float baseVolumeWidth     = 44.0f; // Slider horizontal layout width footprint allocation

        // Arrays to store real-time calculations for EVERY component
        std::vector<float> dynamicWidths;
        std::vector<float> dynamicScales;
        float maxDockHeight = baseSize;

        // -------------------------------------------------------------------------
        // PASS 1: PROGRESSIVE MULTI-PASS COORDINATE RE-ANCHORING
        // -------------------------------------------------------------------------

        float totalCalculatedWidth = 0.0f;        
        for (int convergencePass = 0; convergencePass < 3; ++convergencePass) {
            dynamicWidths.clear();
            dynamicScales.clear();
            maxDockHeight = baseSize;
            
            // Start reading layouts from a relative left offset margin
            float progressiveX = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
            
            // 1. Process standard app launchers & active window indicators (2D SMOOTH FIX)
            for (size_t i = 0; i < totalIconsCount; ++i) {
                float approxCenterX = progressiveX + (baseSize / 2.0f);
                
                // Calculate the visual center point of the icon on the Y axis
                float approxCenterY = fHeight - 10.0f - (baseSize / 2.0f);
                
                // Compute independent delta vectors
                float distanceX = std::abs(fMouseX - approxCenterX);
                float distanceY = std::abs(fMouseY - approxCenterY);
                
                // Calculate true 2D hypotenuse distance from the mouse to the center of the icon
                float distance2D = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                
                float scale = 1.0f;
                // FIX: Base magnification on the total 2D distance sphere (180.0f radius provides excellent glide feel)
                if (fCursorIsInsideHitbox && distance2D < 180.0f) {
                    float ratio = distance2D / 180.0f;
                    
                    // Smooth Gaussian bell-curve falloff transitions perfectly in all directions
                    scale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
                }

                float finalSize = baseSize * scale;
                dynamicWidths.push_back(finalSize);
                dynamicScales.push_back(scale);
                
                if (finalSize > maxDockHeight) maxDockHeight = finalSize;
                progressiveX += finalSize + padding;
            }


            
            if (totalIconsCount > 0) progressiveX -= padding; 

            // Account for the structural native app split divider
            if (activeWindowsCount > 0) {
                progressiveX += separatorGapPadding;
            }

            
            // =========================================================================
            // PROCESS HAIKU TRASH CAN COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            progressiveX += clockSectionPadding;
            float approxTrashCenterX = progressiveX + (baseTrashSize / 2.0f);
            
            // Calculate the spatial center point of the Trash Can icon on the Y axis
            float approxTrashCenterY = fHeight - 10.0f - (baseTrashSize / 2.0f);
            
            // Compute separate directional delta vectors
            float distanceTrashX = std::abs(fMouseX - approxTrashCenterX);
            float distanceTrashY = std::abs(fMouseY - approxTrashCenterY);
            
            // Calculate true 2D distance using the hypotenuse formula
            float distanceTrash2D = std::sqrt(distanceTrashX * distanceTrashX + distanceTrashY * distanceTrashY);
            
            float trashScale = 1.0f;
            // FIX: Rely purely on the 2D radial distance sphere (matching your 180.0f radius baseline)
            if (fCursorIsInsideHitbox && distanceTrash2D < 180.0f) {
                float ratio = distanceTrash2D / 180.0f;
                
                // Smooth Gaussian bell-curve falloff transitions cleanly in all 360 degrees
                trashScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            float finalTrashSize = baseTrashSize * trashScale;
            dynamicWidths.push_back(finalTrashSize);
            dynamicScales.push_back(trashScale);
            if (finalTrashSize > maxDockHeight) maxDockHeight = finalTrashSize;
            progressiveX += finalTrashSize;

 			
   	    	// =========================================================================
	        // DYNAMIC SYSTEM TRAY SLOT WIDTH PARAMETER (2D SMOOTH FIX)
	        // NOTE: Uses 6.0f internal spacing to match your main RenderFrame pipeline!
	        // =========================================================================
	        if (showSystemTray) {
            	// Run our throttled texture sync check
            	SyncDynamicSystrayTextures();
			}
	        float traySectionPadding = clockSectionPadding;
	        size_t trayCount = fLiveTrayItems.size();
	        
            float baselineTrayWidth = 0.0f;
            if (showSystemTray && trayCount > 0) {
                baselineTrayWidth = (trayCount * 16.0f) + ((trayCount > 1 ? trayCount - 1 : 0) * 6.0f);
            }
	
	        progressiveX += traySectionPadding;
	        float approxTrayCenterX = progressiveX + (baselineTrayWidth / 2.0f);
	        
	        // Calculate the standard spatial center point on the Y axis for the tray row
	        float approxTrayCenterY = fHeight - 10.0f - (baseSize / 2.0f);
	        
	        // Compute independent delta vectors
	        float distanceTrayX = std::abs(fMouseX - approxTrayCenterX);
	        float distanceTrayY = std::abs(fMouseY - approxTrayCenterY);
	        
	        // Calculate true 2D distance using the hypotenuse formula
	        float distanceTray2D = std::sqrt(distanceTrayX * distanceTrayX + distanceTrayY * distanceTrayY);
	        
	        float trayScale = 1.0f;
	        // FIX: Base magnification entirely on the 2D radial distance sphere
	        if (fCursorIsInsideHitbox && distanceTray2D < 180.0f) {
	            float ratio = distanceTray2D / 180.0f;
	            
	            // Smooth Gaussian bell-curve falloff transitions cleanly in all directions
	            trayScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        
	        dynamicWidths.push_back(baselineTrayWidth * trayScale);
	        dynamicScales.push_back(trayScale);
	        progressiveX += (baselineTrayWidth * trayScale);
	        // =========================================================================

 			
            // =========================================================================
            // PROCESS SYSTEM CLOCK COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            if (fClockTexture.id != 0) {
                progressiveX += clockSectionPadding;
                
                float highDpiCompensateFactor = 0.42f;
                float baselineClockLayoutWidth = static_cast<float>(fClockWidth) * highDpiCompensateFactor;
                
                float approxClockCenterX = progressiveX + (baselineClockLayoutWidth / 2.0f);
                
                // Calculate spatial center point on the Y axis for the text string element
                float approxClockCenterY = fHeight - 10.0f - (baseSize / 2.0f);
                
                float distanceClockX = std::abs(fMouseX - approxClockCenterX);
                float distanceClockY = std::abs(fMouseY - approxClockCenterY);
                float distanceClock2D = std::sqrt(distanceClockX * distanceClockX + distanceClockY * distanceClockY);
                
                float clockScale = 1.0f;
                // FIX: Base magnification entirely on the unified 180.0f radial distance circle
                if (fCursorIsInsideHitbox && distanceClock2D < 180.0f) {
                    float ratio = distanceClock2D / 180.0f;
                    clockScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
                }
                
                dynamicWidths.push_back(baselineClockLayoutWidth * clockScale);
                dynamicScales.push_back(clockScale);
                progressiveX += (baselineClockLayoutWidth * clockScale);
            } else {
                dynamicWidths.push_back(0.0f);
                dynamicScales.push_back(1.0f);
            }

            // =========================================================================
            // PROCESS DYNAMIC VOLUME SLIDER COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            float layoutSizeRatio = baseSize / 48.0f;
            
            // Proportional Separator
            progressiveX += (clockSectionPadding * layoutSizeRatio);
            
            float scaledBaseVolumeWidth = baseVolumeWidth * layoutSizeRatio;
            float approxVolCenterX = progressiveX + (scaledBaseVolumeWidth / 2.0f);
            float approxVolCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceVolX = std::abs(fMouseX - approxVolCenterX);
            float distanceVolY = std::abs(fMouseY - approxVolCenterY);
            float distanceVol2D = std::sqrt(distanceVolX * distanceVolX + distanceVolY * distanceVolY);
            
            float volScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceVol2D < 180.0f) {
                float ratio = distanceVol2D / 180.0f;
                volScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            dynamicWidths.push_back(scaledBaseVolumeWidth * volScale);
            dynamicScales.push_back(volScale);
            progressiveX += (scaledBaseVolumeWidth * volScale);

            // =========================================================================
            // PROCESS GRAPHICAL CPU MONITOR METRICS (2D SMOOTH FIX)
            // =========================================================================
            // Proportional Separator
            progressiveX += (clockSectionPadding * layoutSizeRatio); 
            
            float scaledCpuGraphWidth = cpuGraphWidth * layoutSizeRatio;
            float approxCpuCenterX = progressiveX + (scaledCpuGraphWidth / 2.0f);
            float approxCpuCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceCpuX = std::abs(fMouseX - approxCpuCenterX);
            float distanceCpuY = std::abs(fMouseY - approxCpuCenterY);
            float distanceCpu2D = std::sqrt(distanceCpuX * distanceCpuX + distanceCpuY * distanceCpuY);
            
            float cpuScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceCpu2D < 180.0f) {
                float ratio = distanceCpu2D / 180.0f;
                cpuScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            float finalCpuWidth = scaledCpuGraphWidth * cpuScale;
            dynamicWidths.push_back(finalCpuWidth);
            dynamicScales.push_back(cpuScale);
            progressiveX += finalCpuWidth;

            float leftEdge = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
            totalCalculatedWidth = progressiveX - leftEdge;
        }

		
		//@here
		//@mouseclick
	    // -------------------------------------------------------------------------
	    // PASS 2: BOUNDS SETTLEMENT AND BACKPLATE GEOMETRY ALLOCATION
	    // -------------------------------------------------------------------------
	    size_t trashSlotIdx   = totalIconsCount;
	    size_t traySlotIdx    = totalIconsCount + 1;
	    size_t clockSlotIdx   = totalIconsCount + 2;
	    size_t volumeSlotIdx  = totalIconsCount + 3;
	    size_t cpuSlotIdx     = totalIconsCount + 4;
	    
	    float dockMarginBottom = 15.0f;
	    HaikuRect dockPlate;
	
	    // =========================================================================
	    // 1:1 RENDER MATCHING: REPLICATE THE EXACT DYNAMIC GEOMETRY
	    // =========================================================================
	    float layoutSizeRatio = baseSize / 48.0f;
	    float leftPaddingbuffer = 1.0f;
	    float internalSidePadding = fBaseIconSize * layoutSizeRatio; 
	    
	    float clippingCompensation = 0.0f;
	    if (layoutSizeRatio > 1.0f) {
	        clippingCompensation = (leftPaddingbuffer * (layoutSizeRatio - 60.0f));
	    }
	
	    // Apply the identical responsive width tracking variable
	    float adjustedTotalWidth = totalCalculatedWidth + clippingCompensation;
	
	    // Balance the dock plate exactly how it is drawn on screen
	    dockPlate.left   = (fWidth / 2.0f) - (adjustedTotalWidth / 2.0f) - internalSidePadding;
	    dockPlate.right  = (fWidth / 2.0f) + (adjustedTotalWidth / 2.0f) + internalSidePadding;
	    dockPlate.bottom = fHeight - dockMarginBottom;
	    dockPlate.top    = dockPlate.bottom - maxDockHeight - 20.0f;
	
	    // Lock down definitive trash hitbox using converged data fields
	    float renderingTrashSize = dynamicWidths[trashSlotIdx];
	    
	    float layoutTrackerX = dockPlate.left + 20.0f;
	
	    for (size_t idx = 0; idx < totalIconsCount; ++idx) {
	        layoutTrackerX += dynamicWidths[idx] + padding;
	    }
	    if (totalIconsCount > 0) layoutTrackerX -= padding;
	    if (activeWindowsCount > 0) layoutTrackerX += separatorGapPadding;
	    
	    // SYNCHRONIZED: Account for the horizontal width footprint of the System Tray slot
	    layoutTrackerX += clockSectionPadding + dynamicWidths[traySlotIdx];
	    
	    if (fClockTexture.id != 0) layoutTrackerX += clockSectionPadding + dynamicWidths[clockSlotIdx];
	    
	    // Accountability shift step past our volume metrics
	    layoutTrackerX += clockSectionPadding + dynamicWidths[volumeSlotIdx];       
	    layoutTrackerX += clockSectionPadding;
	    
	    fTrashRect.left = layoutTrackerX;
	    fTrashRect.right = fTrashRect.left + renderingTrashSize;
	    fTrashRect.top = dockPlate.bottom - 10.0f - renderingTrashSize;
	    fTrashRect.bottom = dockPlate.bottom - 10.0f;
	
	    // =========================================================================
	    // PROGRESSIVE STRUCTURAL ROUTING INTERCEPTOR (1:1 GEOMETRY MATCH)
	    // =========================================================================
	    // The exact starting visual layout anchor coordinate used in pass 5 of rendering
	    float currentX = dockPlate.left + 20.0f; 
	    size_t evaluationSlotIdx = 0;

	    // STEP A: EVALUATE BASELINE SYSTEM LAUNCHERS (MENU LEAF + FILE SHORTCUTS)
	    for (size_t i = 0; i < baselineLaunchersCount; ++i) {
	        float size = dynamicWidths[evaluationSlotIdx];
        
	        // Correctly calculate visual height baseline boundary metrics
	        HaikuRect realIconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
	    
	        if (x >= realIconBounds.left && x <= realIconBounds.right &&
	            y >= realIconBounds.top  && y <= realIconBounds.bottom) {
	                
		            if (i == 0) {
		            // =========================================================================
		            // LEAF ICON RIGHT-CLICK: ASYNCHRONOUS NON-BLOCKING POPUP ENGINE
		            // =========================================================================			
		                if (button == SDL_BUTTON_RIGHT) {                                  
		                    if (fLeafMenuIsActive) return;
		                    fLeafMenuIsActive = true;
		
		                    // FIX 1: Declared exactly ONCE so initialization properties are preserved
		                    LeafMenuArgs* args = new LeafMenuArgs();
		                    args->engine = this;
		                    args->winX = 0; 
		                    args->winY = 0;
		                    args->mouseX = static_cast<int32>(x);
		                    
		                    // Match the baseline dynamic scaling formula used by the window sizing logic
		                    args->currentDockH = static_cast<float>(std::ceil(fBaseIconSize * 3.5f)); 
		
		                    if (be_app && be_app->Lock()) {
		                        BWindow* mainNativeWin = be_app->WindowAt(0);
		                        if (mainNativeWin != nullptr) {
		                            args->winX = static_cast<int32>(mainNativeWin->Frame().left);
		                            args->winY = static_cast<int32>(mainNativeWin->Frame().top);
		                        }
		                        be_app->Unlock();
		                    }
		
		                    // Inline background thread function context
		                    int32 (*inlineLeafThreadFunc)(void*) = [](void* data) -> int32 {
		                        LeafMenuArgs* threadArgs = static_cast<LeafMenuArgs*>(data);
		                        if (!threadArgs || !threadArgs->engine) {
		                            if (threadArgs) delete threadArgs;
		                            return B_ERROR;
		                        }
		
		                        BPopUpMenu* leafMenu = new BPopUpMenu("LeafPopup", false, false);
		                        leafMenu->SetRadioMode(false);
		                        leafMenu->AddItem(new BMenuItem("Preferences…", new BMessage('lCFG')));
		                        
		                        float anchoredMenuX = static_cast<float>(threadArgs->winX + threadArgs->mouseX) - 15.0f;
		                        if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f; 
		
		                        // FIX 2: Correct layout normalization metrics boundary limit tracker.
		                        // Assuming 164.0f is your standard maximum baseline footprint width layout,
		                        // this naturally pushes the layout down when currentDockH drops to ~116.0f.
		                        float maxExpectedHeight = 164.0f; 
		                        float structuralOffset = maxExpectedHeight - threadArgs->currentDockH;
		                        if (structuralOffset < 0.0f) structuralOffset = 0.0f; // Safety clamp prevent clipping
		                        
		                        // Push it lower down screen boundary context as your dock container shrivels
		                        float anchoredMenuY = static_cast<float>(threadArgs->winY) + structuralOffset - 5.0f; 
		                        
		                        BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);
		
		                        BMenuItem* chosenAction = leafMenu->Go(screenClickPoint, false, false);
		                        
		                        threadArgs->engine->fLastLeafMenuCloseTime = SDL_GetTicks();
		                        threadArgs->engine->fLeafMenuIsActive = false; 
								//@here
		                        if (chosenAction != nullptr && chosenAction->Message() != nullptr) {
									if (chosenAction->Message()->what == 'lCFG') {
									    float winWidth = 560.0f;
									    float winHeight = 450.0f; // Expanded to 450 pixels!
									
									    BScreen screen(B_MAIN_SCREEN_ID);
									    BRect screenFrame = screen.Frame();
									    
									    float centerX = screenFrame.left + (screenFrame.Width() - winWidth) / 2.0f;
									    float centerY = screenFrame.top + (screenFrame.Height() - winHeight) / 2.0f;
									    BRect centeredBounds(centerX, centerY, centerX + winWidth, centerY + winHeight);
									    
									    BWindow* settingsWindow = new BWindow(centeredBounds, "hdesktop settings", 
									        B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE);
									    
									    settingsWindow->AddChild(new ConfigView(settingsWindow->Bounds()));
									    settingsWindow->Show();
									}
		                        }
		
		                        delete leafMenu;
		                        delete threadArgs; 
		                        return B_OK;
		                    };

                    thread_id menuThread = spawn_thread(inlineLeafThreadFunc, "async_leaf_menu", B_NORMAL_PRIORITY, args);
                    if (menuThread >= B_OK) {
                        resume_thread(menuThread);
                    } else {
                        fLeafMenuIsActive = false;
                        delete args;
                    }
                }
                // =========================================================================
                // LEAF ICON LEFT-CLICK: TOGGLE NATIVE MAIN DRAWER (ORIGINAL PIPELINE)
                // =========================================================================

                else {
                    if (gActiveDrawerInstance != nullptr) {                    
                        if (gActiveDrawerInstance->Lock()) {
                            gActiveDrawerInstance->Quit(); 
                        }
                    } 
                    else {                   
                        gActiveDrawerInstance = new HaikuAppDrawerWindow(fHeight);
                        gActiveDrawerInstance->Show();
                    }
                    
                    fShowMainMenu = false; 
                }
            } 
            return;

        }

	        currentX += size + padding;
	        evaluationSlotIdx++;
	    }

	
	    // Account for horizontal visual taskbar line padding offsets split
	    if (activeWindowsCount > 0) {
	        currentX += separatorGapPadding;
	    }
	    
	    // STEP B: EVALUATE LIVE OPEN RUNNING TASKBAR WINDOW APP TOGGLES
	    for (size_t w = 0; w < fTaskbarWindows.size(); ++w) {
	        auto& activeTaskWin = fTaskbarWindows[w];
	        if (*activeTaskWin.openStateFlag == false) continue;
	
	        float size = dynamicWidths[evaluationSlotIdx];
	        HaikuRect realIconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
	        
	        // --- TRACKER SAFETY TOGGLE PIPELINES ---
	        bool isTracker = false;
	        app_info appInfo;
	        if (be_roster->GetRunningAppInfo(activeTaskWin.teamId, &appInfo) == B_OK) {
	            if (strcmp(appInfo.signature, "application/x-vnd.Be-TRAK") == 0) isTracker = true;
	        }
	
	        if (x >= realIconBounds.left && x <= realIconBounds.right &&
	            y >= realIconBounds.top  && y <= realIconBounds.bottom) {
	            // =========================================================================
	            // MIDDLE MOUSE CLICK: NATIVE APPLICATION CLOSE PROTOCOL (FIXED BUTTONS)
	            // =========================================================================
	            // FIX: Removed !isTracker condition to let the middle click target Tracker
	            	            if (button == SDL_BUTTON_MIDDLE && button != SDL_BUTTON_RIGHT) {
	                
	             	if (isTracker) {
	                    BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	                    if (trackerMessenger.IsValid()) {
	                        BMessage countRequest(B_COUNT_PROPERTIES);
	                        countRequest.AddSpecifier("Window");
	                        
	                        BMessage reply;
	                        if (trackerMessenger.SendMessage(&countRequest, &reply) == B_OK) {
	                            int32 totalWindows = 0;
	                            if (reply.FindInt32("result", &totalWindows) == B_OK) {	                                
	                                
	     	                        // Loop backwards through windows, but STOP before Index 0 
	                                // Index 0 is Tracker's critical internal framework window; quitting it kills the server!
	                                for (int32 wIdx = totalWindows - 1; wIdx > 0; --wIdx) {
	                                    
	                                    // Request the title of the window first so we can evaluate it
	                                    BMessage titleRequest(B_GET_PROPERTY);
	                                    titleRequest.AddSpecifier("Title");
	                                    titleRequest.AddSpecifier("Window", wIdx);
	                                    
	                                    BMessage titleReply;
	                                    BString winTitle = "";
	                                    if (trackerMessenger.SendMessage(&titleRequest, &titleReply) == B_OK) {
	                                        const char* nameStr = nullptr;
	                                        if (titleReply.FindString("result", &nameStr) == B_OK && nameStr != nullptr) {
	                                            winTitle = nameStr;
	                                        }
	                                    }
	                                   
	                                    // CRITICAL SAFETY FILTER A: Drop out instantly on blank names or status panels
	                                    if (winTitle.Length() == 0 || winTitle == "Tracker status") {
	                                        continue; 
	                                    }

	                                    // CRITICAL SAFETY FILTER B: Protect the system background backdrop layer
	                                    if (winTitle == "Desktop") {
	                                        continue; // Protect Tracker backdrop from crashing
	                                    }
	                                    
	                                    // Safe to target user windows at indices > 0 with a Quit request
	                                    BMessage quitWindowMessage(B_QUIT_REQUESTED);
	                                    quitWindowMessage.AddSpecifier("Window", wIdx);
	                                    trackerMessenger.SendMessage(&quitWindowMessage);
	                                }
	                            }
	                        }
	                    }
	                }

	                else {
	                    // GENERAL APPLICATIONS: Standard clean closure sequence	                    
	                    BMessenger targetAppMessenger(NULL, activeTaskWin.teamId);
	                    if (targetAppMessenger.IsValid()) {
	                        targetAppMessenger.SendMessage(B_QUIT_REQUESTED);
	                    } else {
	                        kill_team(activeTaskWin.teamId);
	                    }
	                }
	                
	                fShowMainMenu = false;
	                return; 
	            }



                 if (fShowMainMenu) {
                        BWindow* nativeWin = be_app->WindowAt(0);
                        if (nativeWin != nullptr && nativeWin->Lock()) {
                            nativeWin->SetFeel(B_NORMAL_WINDOW_FEEL);
                            nativeWin->ResizeBy(0, -220.0f);
                            nativeWin->MoveBy(0, 220.0f);
                            nativeWin->Unlock();
                        }
                    }
                


	            fShowMainMenu = false;

	
				if (isTracker) {
				    // --- FIX: ONLY RIGHT CLICK SPAWNS THE ASYNC POPUP MENU ---
				    // =========================================================================
				    if (button == SDL_BUTTON_RIGHT) {
				        uint32 currentClickTick = SDL_GetTicks();
				        
				        // SEQUENTIAL TOGGLE SHIELD: Check rapid click limits
				        if (currentClickTick - fLastTrackerMenuCloseTime < 150) {
				            fLastTrackerMenuCloseTime = 0; 
				            return; 
				        }
				    
				        // ACTIVE MENU CLOSE CHECK: If the menu is currently visible and they click again, close it
				        if (fTrackerMenuIsActive) {
				            return; 
				        }
				    
				        fTrackerMenuIsActive = true; // Engage active state safety latch
				    
				        TrackerMenuArgs* args = new TrackerMenuArgs();
				        args->engine = this; // Pass engine instance pointer safely
				        args->winX = 0;
				        args->winY = 0;
				        args->mouseX = static_cast<int32>(x); 
				        
				        // SMART MATCH: Mirror the exact dynamic layout sizing math 
				        args->currentDockH = static_cast<float>(std::ceil(fBaseIconSize * 3.5f)); 

				        // SMART NAVIGATION: Query native Haiku window coordinates like preferences popup
				        if (be_app && be_app->Lock()) {
				            BWindow* mainNativeWin = be_app->WindowAt(0);
				            if (mainNativeWin != nullptr) {
				                args->winX = static_cast<int32>(mainNativeWin->Frame().left);
				                args->winY = static_cast<int32>(mainNativeWin->Frame().top);
				            }
				            be_app->Unlock();
				        }
				    
				        thread_id menuThread = spawn_thread(SpawnTrackerMenuThread, "async_tracker_menu", B_NORMAL_PRIORITY, args);
				        if (menuThread >= B_OK) {
				            resume_thread(menuThread);
				        } else {
				            fTrackerMenuIsActive = false;
				            delete args;
				        }
				    
				        return; // Intercept right click so it doesn't fire window minimize/restore macros
				    }
				    // =========================================================================
				}


				

					
		    // GENERAL APPLICATION BEHAVIOR (NON-TRACKER APPS)
	            #ifndef AS_MINIMIZE_TEAM
	            #define AS_MINIMIZE_TEAM 5
	            #endif
	            #ifndef AS_BRING_TEAM_TO_FRONT
	            #define AS_BRING_TEAM_TO_FRONT 6
	            #endif
	
                // =========================================================================
                // SINGLE-CLICK LATCH GUARD: Blocks holding down the mouse button from re-triggering
                // =========================================================================
                static std::map<std::pair<team_id, int32>, bool> isButtonLatchedMap;
                std::pair<team_id, int32> actionKey(activeTaskWin.teamId, static_cast<int32>(w));

                if (isButtonLatchedMap[actionKey]) {
                    return; 
                }
                
                isButtonLatchedMap[actionKey] = true;
                // =========================================================================

                // READ TRUTH FROM RENDERFRAME WORKSPACE BITMASK
	            if (activeTaskWin.isMinimized == false) {
                
	                BPrivate::AppServerLink link;
	                link.StartMessage(AS_MINIMIZE_TEAM);
	                link.Attach<team_id>(activeTaskWin.teamId);
	                link.Flush();
	                
	                be_roster->ActivateApp(-1);
	                // FIX 1: Removed activeTaskWin.isMinimized = true; 
                    // Let RenderFrame handle state evaluation on the next frame pass.
	            } 
	            else {
	                if (isTracker && button == SDL_BUTTON_LEFT) {
	                    int32 fileFolderCount = 0;
	                    int32 tokenCount = 0;
	                    int32* tokens = get_token_list(activeTaskWin.teamId, &tokenCount);
	                    
	                    if (tokens != nullptr) {
	                        for (int32 i = 0; i < tokenCount; ++i) {
	                            client_window_info* wInfo = get_window_info(tokens[i]);
	                            if (wInfo != nullptr) {
	                                if (wInfo->name[0] != '\0' && strcmp(wInfo->name, "Tracker status") != 0) {
	                                    // If the window name is "Desktop", check if it is a real folder window or the system background.
	                                    // B_DESKTOP_WINDOW_FEEL has a constant value of 1024.
	                                    if (strcmp(wInfo->name, "Desktop") == 0 || BString(wInfo->name).EndsWith("/Desktop")) {
	                                        // If it is NOT the background layer (feel 1024), count it as an open folder window!
	                                        if (wInfo->feel != 1024) {
	                                            fileFolderCount++;
	                                        }
	                                    } else {
	                                        // Count any other standard tracker directory windows
	                                        fileFolderCount++;
	                                    }
	                                }
	                                free(wInfo);
	                            }
	                        }
	                        free(tokens);
	                    }

	                    if (fileFolderCount == 0) {
	                        BEntry entry("/boot/home");
	                        entry_ref ref;
	                        if (entry.GetRef(&ref) == B_OK) {
	                            BMessage message(B_REFS_RECEIVED);
	                            message.AddRef("refs", &ref);
	                            be_roster->Launch("application/x-vnd.Be-TRAK", &message);
	                        }
	                        
	                        isButtonLatchedMap[actionKey] = false; 
	                        return; 
	                    }
	                }

	                
	                app_info targetAppInfo;
	                if (be_roster->GetRunningAppInfo(activeTaskWin.teamId, &targetAppInfo) == B_OK) {
	                    be_roster->ActivateApp(targetAppInfo.team);
	                } else {
	                    be_roster->ActivateApp(activeTaskWin.teamId);
	                }
	                
                    // FIX 2: Group Restore Force Pipeline.
                    // This explicitly flushes window tokens belonging to group-minimized layers 
                    // (like Pe or WebPositive) back onto the active workspace array.
	                BPrivate::AppServerLink link;
	                link.StartMessage(AS_BRING_TEAM_TO_FRONT);
	                link.Attach<team_id>(activeTaskWin.teamId);
	                link.Flush();
	                
                    // Force focus routing loop to active window tokens to trigger layout updates
                    int32 systemCount = 0;
                    int32 currentWorkspace = current_workspace();
                    int32* systemTokens = nullptr;
                    if (BPrivate::get_window_order(currentWorkspace, &systemTokens, &systemCount) == B_OK && systemTokens != nullptr) {
                        for (int32 i = 0; i < systemCount; ++i) {
                            client_window_info* cInfo = get_window_info(systemTokens[i]);
                            if (cInfo != nullptr) {
                                if (cInfo->team == activeTaskWin.teamId && cInfo->feel == B_NORMAL_WINDOW_FEEL) {
                                    // Inject sub-message to force individual window wakeups
                                    BPrivate::AppServerLink winLink;
                                    winLink.StartMessage(AS_BRING_TEAM_TO_FRONT);
                                    winLink.Attach<int32>(systemTokens[i]);
                                    winLink.Flush();
                                }
                                free(cInfo);
                            }
                        }
                        free(systemTokens);
                    }

	                // FIX 1: Removed activeTaskWin.isMinimized = false;
                    // Let RenderFrame handle state evaluation on the next frame pass.
	            }
	            
                
                isButtonLatchedMap[actionKey] = false;
	            return; 

	        }
	        
	        currentX += size + padding;
	        evaluationSlotIdx++;

	    }


	    // =========================================================================
	    // STEP C: EVALUATE SYSTEM TRAY COMPONENTS ( TRASH BIN -> TRAY -> CLOCK -> VOLUME -> CPU)
	    // =========================================================================	    
	    
	    // -------------------------------------------------------------------------
	    // Evaluate Click Bounds for Haiku Trash Bin Component
	    // -------------------------------------------------------------------------

	    currentX += clockSectionPadding;
	    float dynamicTrashSize = dynamicWidths[trashSlotIdx];
	    HaikuRect trashBounds = { currentX, dockPlate.bottom - 10.0f - dynamicTrashSize, currentX + dynamicTrashSize, dockPlate.bottom - 10.0f };
	    
	    if (x >= trashBounds.left && x <= trashBounds.right &&
	        y >= trashBounds.top  && y <= trashBounds.bottom) {
	        
	        if (fShowMainMenu) {
	            BWindow* nativeWin = be_app->WindowAt(0);
	            if (nativeWin != nullptr && nativeWin->Lock()) {
	                nativeWin->SetFeel(B_NORMAL_WINDOW_FEEL);
	                nativeWin->ResizeBy(0, -220.0f);
	                nativeWin->MoveBy(0, 220.0f);
	                nativeWin->Unlock();
	            }
	        }
	        fShowMainMenu = false;
	
	        if (button == SDL_BUTTON_LEFT) {
	            std::system("/boot/system/Tracker /boot/trash &");
	            return;
	        }
	        else if (button == SDL_BUTTON_MIDDLE) {
	            std::system("trash --empty &"); 
	            fLastTrashCheckTime = 0; 
	            return;
	        }
	        else if (button == SDL_BUTTON_RIGHT) {
	            uint32 currentClickTick = SDL_GetTicks();
	            
	            // Reused sequential toggle shield
	            if (currentClickTick - fLastTrackerMenuCloseTime < 150) {
	                fLastTrackerMenuCloseTime = 0; 
	                return; 
	            }
	
	            // Reused active menu latch check
	            if (fTrackerMenuIsActive) {
	                return; 
	            }
	
	            fTrackerMenuIsActive = true; 
	
	            TrackerMenuArgs* args = new TrackerMenuArgs();
	            args->engine = this; 
	            args->winX = 0;
	            args->winY = 0;
	            args->mouseX = static_cast<int32>(x); 
	            args->mouseY = static_cast<int32>(y); 
	            
	            // SMART MATCH: Pass the live unified dynamic scaling height parameter down
	            args->currentDockH = static_cast<float>(std::ceil(fBaseIconSize * 3.5f));

	            // SMART NAVIGATION: Lock native Haiku app frames rather than querying raw SDL views
	            if (be_app && be_app->Lock()) {
	                BWindow* mainNativeWin = be_app->WindowAt(0);
	                if (mainNativeWin != nullptr) {
	                    args->winX = static_cast<int32>(mainNativeWin->Frame().left);
	                    args->winY = static_cast<int32>(mainNativeWin->Frame().top);
	                }
	                be_app->Unlock();
	            }
	
	            // SELF-CONTAINED INLINE THREAD POINTER
	            int32 (*inlineThreadFunc)(void*) = [](void* data) -> int32 {
	                TrackerMenuArgs* threadArgs = static_cast<TrackerMenuArgs*>(data);
	                if (!threadArgs || !threadArgs->engine) {
	                    delete threadArgs;
	                    return B_ERROR;
	                }
	
	                // Establish a native messenger channel directly to Tracker
	                BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	
	                BPopUpMenu* trashMenu = new BPopUpMenu("TrashPopup", false, false);
	                trashMenu->SetRadioMode(false);
	                
	                // Streamlined to match the exact 2 clean operational choices
	                BMenuItem* emptyItem = new BMenuItem("Empty Trash", new BMessage('mEMP')); 
	                BMenuItem* openItem  = new BMenuItem("Open", new BMessage(B_REFS_RECEIVED), 'O');
	                
	                trashMenu->AddItem(emptyItem);
	                trashMenu->AddItem(openItem);
	                
	                // EXACT TRACKER POSITION MATCHING
	                float anchoredMenuX = static_cast<float>(threadArgs->winX + threadArgs->mouseX) - 45.0f;
	                if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f;
	                
	                // SMART POSITIONING: Normalize menu coordinates based on dynamic dock boundaries
	                float maxExpectedHeight = 164.0f; 
	                float structuralOffset = maxExpectedHeight - threadArgs->currentDockH;
	                if (structuralOffset < 0.0f) structuralOffset = 0.0f; // Clamp shield protection
	                
	                // Apply dynamic tracking pushing popup lower down when icons shrink
	                float anchoredMenuY = static_cast<float>(threadArgs->winY) + structuralOffset - 5.0f; 
	                BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);
	
	                // Open synchronously inside our background thread
	                BMenuItem* chosenAction = trashMenu->Go(screenClickPoint, false, false);
	                
	                // Reset active safety flags
	                threadArgs->engine->fLastTrackerMenuCloseTime = SDL_GetTicks();
	                threadArgs->engine->fTrackerMenuIsActive = false; 
	
	                // PROCESS SELECTIONS VIA TRACKER MESSENGER LOOP

	                if (chosenAction != nullptr && chosenAction->Message() != nullptr) {
	                    uint32 command = chosenAction->Message()->what;
	                    
	                    if (command == B_REFS_RECEIVED) {
	                        entry_ref ref;
	                        if (get_ref_for_path("/boot/trash", &ref) == B_OK) {
	                            BMessage openMsg(B_REFS_RECEIVED);
	                            openMsg.AddRef("refs", &ref);
	                            if (trackerMessenger.IsValid()) {
	                                trackerMessenger.SendMessage(&openMsg);
	                            }
	                        }
	                    } 
	                    else if (command == 'mEMP') {
	                        std::system("trash --empty &");
	                    }
	                }
	
	                delete trashMenu;
	                delete threadArgs; 
	                return B_OK;
	            };
	
	            thread_id menuThread = spawn_thread(inlineThreadFunc, "async_trash_menu", B_NORMAL_PRIORITY, args);
	            if (menuThread >= B_OK) {
	                resume_thread(menuThread);
	            } else {
	                fTrackerMenuIsActive = false;
	                delete args;
	            }
	
	            return; 
	        }
	
	
	    } 
	    currentX += dynamicTrashSize;

	    // -------------------------------------------------------------------------
	    // INTERCEPT AND EVALUATE CLICK BOUNDS FOR THE REPLICANT SYSTEM TRAY
	    // -------------------------------------------------------------------------
	    float dynamicTrayWidth = dynamicWidths[traySlotIdx];
	    float trayScaleFactor  = dynamicScales[traySlotIdx];
	    
	    // Only process tray metrics and click captures if the global config is enabled
	    if (showSystemTray) {
	        currentX += clockSectionPadding;
	        
	        // Match the 16px high tray vertical position bounds used in RenderFrame
	        HaikuRect trayHitbox = { 
	            currentX, 
	            dockPlate.bottom - 10.0f - (32.0f * trayScaleFactor), 
	            currentX + dynamicTrayWidth, 
	            dockPlate.bottom - 10.0f 
	        };
	
	        if (x >= trayHitbox.left && x <= trayHitbox.right && y >= trayHitbox.top && y <= trayHitbox.bottom) {
	            float localTrayTrackerX = currentX;
	            float iconHitboxSize = 16.0f * trayScaleFactor;
	            float traySpacing = 6.0f * trayScaleFactor;
	
	            // Dynamically sweep over your live running system tray list
	            for (size_t t = 0; t < fLiveTrayItems.size(); ++t) {
	                float itemLeft  = localTrayTrackerX;
	                float itemRight = itemLeft + iconHitboxSize;
	
	                if (x >= itemLeft && x <= itemRight) {
	                    BMessenger deskbarMessenger("application/x-vnd.be-tskb");
	                    if (deskbarMessenger.IsValid()) {
	                        
	                        // =========================================================================
	                        // CASE 1: RIGHT-CLICK -> PARSE AND PRESENT REAL CONTEXT DROPDOWNS (STABLE)
	                        // =========================================================================
	                        if (button == SDL_BUTTON_RIGHT) {	
	                            BPopUpMenu* localMenu = new BPopUpMenu("SystrayContext", false, false);
	                            bool foundItems = false;
	
	                            BMessage menuRequest(B_GET_PROPERTY);
	                            menuRequest.AddSpecifier("Menu");
	                            menuRequest.AddSpecifier("Replicant", fLiveTrayItems[t].name.c_str());
	                            menuRequest.AddSpecifier("View", "Status");
	                            menuRequest.AddSpecifier("View", "Deskbar");
	
	                            BMessage menuReply;
	                            if (deskbarMessenger.SendMessage(&menuRequest, &menuReply) == B_OK) {
	                                BMessage archivedItem;
	                                int32 itemIdx = 0;
	
	                                while (menuReply.FindMessage("item", itemIdx, &archivedItem) == B_OK || 
	                                       menuReply.FindMessage("_items", itemIdx, &archivedItem) == B_OK) {
	                                    const char* label = nullptr;
	                                    if (archivedItem.FindString("label", &label) == B_OK && label != nullptr) {
	                                        BMessage* forwardMsg = new BMessage(archivedItem);
	                                        localMenu->AddItem(new BMenuItem(label, forwardMsg));
	                                        foundItems = true;
	                                    }
	                                    archivedItem.MakeEmpty();
	                                    itemIdx++;
	                                }
	                            }
	
	                            if (!foundItems) {
	                                if (fLiveTrayItems[t].name == "ProcessController" || fLiveTrayItems[t].name == "ProcessControllerView") {
	                                    localMenu->AddItem(new BMenuItem("Open Performance Monitor...", new BMessage('act1')));
	                                    localMenu->AddItem(new BMenuItem("Memory Usage Profiles...", new BMessage('act2')));
	                                } else if (fLiveTrayItems[t].name == "NetworkStatus") {
	                                    localMenu->AddItem(new BMenuItem("Open Network Preferences...", new BMessage('net1')));
	                                } else if (fLiveTrayItems[t].name == "MediaReplicant") {
	                                    localMenu->AddItem(new BMenuItem("Open Audio Mixer Preferences...", new BMessage('aud1')));
	                                }
	                            }
	
	                            // Get accurate native window coordinates
	                            int32 nativeWinX = 0;
	                            int32 nativeWinY = 0;
	                            if (be_app && be_app->Lock()) {
	                                BWindow* mainNativeWin = be_app->WindowAt(0);
	                                if (mainNativeWin != nullptr) {
	                                    nativeWinX = static_cast<int32>(mainNativeWin->Frame().left);
	                                    nativeWinY = static_cast<int32>(mainNativeWin->Frame().top);
	                                }
	                                be_app->Unlock();
	                            }
	                            
	                            // Apply the working dynamic scaling calculations
	                            float currentDockH = static_cast<float>(std::ceil(fBaseIconSize + 68.0f));
	                            float maxExpectedHeight = 164.0f; 
	                            float structuralOffset = maxExpectedHeight - currentDockH;
	                            if (structuralOffset < 0.0f) structuralOffset = 0.0f; 
	                            
	                            // STABILITY FIX 1: Anchor X precisely to the physical icon column (localTrayTrackerX) 
	                            // instead of the volatile mouse coordinate pointer.
	                            float anchoredMenuX = static_cast<float>(nativeWinX + localTrayTrackerX) + (iconHitboxSize / 2.0f) - 45.0f;
	                            if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f;
	                            
	                            float anchoredMenuY = static_cast<float>(nativeWinY) + structuralOffset - 5.0f; 
	                            BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);
	
	                            // STABILITY FIX 2: Added explicit parameters down to Go()
	                            // false = Don't auto-send messages immediately (let our loop process it)
	                            // true  = OPEN ANYWAY. Forces the menu to stay open and ignore mouse-up glitches!
	                            BMenuItem* chosenItem = localMenu->Go(screenClickPoint, false, true);
	
	                            if (chosenItem != nullptr) {
	                                BMessage* choiceAction = chosenItem->Message();
	                                if (choiceAction != nullptr) {
	                                    if (choiceAction->what == 'act1') {
	                                        // std::system("/boot/system/apps/ProcessController &");
	                                    } else if (choiceAction->what == 'act2') {
	                                        std::system("/boot/system/apps/ActivityMonitor &");
	                                    } else if (choiceAction->what == 'net1') {
	                                        std::system("/boot/system/preferences/Network &");
	                                    } else if (choiceAction->what == 'aud1') {
	                                        std::system("/boot/system/preferences/Media &");
	                                    } else {
	                                        BMessenger replicantTarget("application/x-vnd.be-tskb");
	                                        replicantTarget.SendMessage(choiceAction);
	                                    }
	                                }
	                            }
	                            delete localMenu; 
	                        }


	                        
	                        // CASE 2: LEFT-CLICK -> PANEL SHORTCUT LAUNCHERS
	                        else if (button == SDL_BUTTON_LEFT) {
	                            if (fLiveTrayItems[t].name == "NetworkStatus") {
	                                std::system("/boot/system/preferences/Network &");
	                            } else if (fLiveTrayItems[t].name == "MediaReplicant") {
	                                std::system("/boot/system/preferences/Media &");
	                            } else if (fLiveTrayItems[t].name == "ProcessController" || fLiveTrayItems[t].name == "ProcessControllerView") {
	                               // std::system("/boot/system/apps/ProcessController &");
	                            } else if (fLiveTrayItems[t].name == "SuperMusicTrayIcon") {
	                                std::system("/boot/system/apps/HaikuSuperMusicThingy &");
	                            }
	                        }
	                    }
	                    fShowMainMenu = false;
	                    return; 
	                }
	                localTrayTrackerX += iconHitboxSize + traySpacing;
	            }
	        }
	        currentX += dynamicTrayWidth;
	    }
	    
	    // -------------------------------------------------------------------------
	    // Evaluate Click Bounds for System Clock Component
	    // -------------------------------------------------------------------------
	    // Establish the universal scaling ratio to keep all components in sync
	    float sizeRatio = static_cast<float>(fBaseIconSize) / 48.0f;

	    if (fClockTexture.id != 0) {
	        // Apply size ratio to match exact rendering dimensions
	        float dynamicClockW = dynamicWidths[clockSlotIdx] * sizeRatio;
	        currentX += clockSectionPadding;
	        
	        HaikuRect clockBounds = { currentX, dockPlate.top, currentX + dynamicClockW, dockPlate.bottom };
	        if (x >= clockBounds.left && x <= clockBounds.right && y >= clockBounds.top && y <= clockBounds.bottom) {
	            std::system("/boot/system/preferences/Time &"); 
	            return;
	        }
	        currentX += dynamicClockW;
	    }
	
	    // -------------------------------------------------------------------------
	    // Skip past Volume Slider component space footprint natively (WITH SIZE RATIO)
	    // -------------------------------------------------------------------------
	    // Multiplying this by sizeRatio prevents the layout tracking from shifting out of position
	    currentX += clockSectionPadding + (dynamicWidths[volumeSlotIdx] * sizeRatio);

	    // -------------------------------------------------------------------------
	    // Evaluate Click Bounds for Graphical LED CPU Monitor Component
	    // -------------------------------------------------------------------------
	    currentX += clockSectionPadding;
	    
	    float dynamicGraphWidth = dynamicWidths[cpuSlotIdx];
	    
	    HaikuRect cpuBounds = { 
	        currentX, 
	        dockPlate.top, 
	        currentX + dynamicGraphWidth, 
	        dockPlate.bottom 
	    };
	    
	    if (x >= cpuBounds.left && x <= cpuBounds.right && y >= cpuBounds.top && y <= cpuBounds.bottom) {
	        if (fCpuMenuIsActive) {
	            return; 
	        }
	
	        if (button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT) {
	            fCpuMenuIsActive = true; 
	
	            CpuMenuArgs* args = new CpuMenuArgs();
	            args->engine = this;
	            args->winX = 0;
	            args->winY = 0;
	            args->mouseX = static_cast<int32>(x);
	            args->mouseY = static_cast<int32>(y);
	            args->currentDockH = static_cast<float>(std::ceil(fBaseIconSize * 3.5f));

	            if (be_app && be_app->Lock()) {
	                BWindow* mainNativeWin = be_app->WindowAt(0);
	                if (mainNativeWin != nullptr) {
	                    args->winX = static_cast<int32>(mainNativeWin->Frame().left);
	                    args->winY = static_cast<int32>(mainNativeWin->Frame().top);
	                }
	                be_app->Unlock();
	            }
	
	            new AsyncCpuMenuRunner(args);
	        }
	        return;
	    }

	    currentX += dynamicGraphWidth;

	} // HandleMouseClick end closing brace




	void SetHaikuMixerVolume(float targetVolumeFraction) {
	    BMediaRoster* roster = BMediaRoster::Roster();
	    if (!roster) return;
	
	    media_node mixerNode;
	    if (roster->GetAudioMixer(&mixerNode) == B_OK) {
	        BParameterWeb* parameterWeb = nullptr;
	        if (roster->GetParameterWebFor(mixerNode, &parameterWeb) == B_OK && parameterWeb != nullptr) {
	            int32 count = parameterWeb->CountParameters();
	            for (int32 i = 0; i < count; i++) {
	                BParameter* param = parameterWeb->ParameterAt(i);
	                if (param && (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) &&
	                    (strcmp(param->Kind(), B_MASTER_GAIN) == 0 || strcmp(param->Name(), "Master") == 0)) {
	                    
	                    BContinuousParameter* gainSlider = static_cast<BContinuousParameter*>(param);
	                    float minGain = gainSlider->MinValue();
	                    float maxGain = gainSlider->MaxValue();
	                    
	                    // Convert the clean 0.0f - 1.0f percentage scale back into raw DB hardware scale factors
	                    float targetGainDb = minGain + (targetVolumeFraction * (maxGain - minGain));
	                    
	                    // Instruct Haiku's Media Server to apply the new gain limits instantly
	                    gainSlider->SetValue(&targetGainDb, sizeof(float), system_time());
	                    break;
	                }
	            }
	            delete parameterWeb;
	        }
	        roster->ReleaseNode(mixerNode);
	    }
	}

                	

   void HandleMouseInput(int x, int y, Uint32 buttonState) {    	
        fMouseX = x; fMouseY = y;
        fIsResizing = false;
       if (fShowMainMenu && !fMainMenuBounds.Contains(x, y)) {
           if (y < fHeight - 140.0f) fShowMainMenu = false;
       }        
    }



	void DrawGLRoundedRect(HaikuRect bounds, float radius, float r, float g, float b, float a, bool fill) {
	    int segments = 8; // Number of vertex points per corner circle slice
	    
	    if (fill) {
	        glBegin(GL_TRIANGLE_FAN);
	    } else {
	        glBegin(GL_LINE_LOOP);
	    }
	    
	    glColor4f(r, g, b, a);
	
	    // Array of corner coordinate anchors to loop through logically
	    struct Corner { float x, y, startAngle; } corners[4] = {
	        { bounds.right - radius, bounds.top + radius,    0.0f * static_cast<float>(M_PI) / 2.0f }, // Top-Right
	        { bounds.left + radius,  bounds.top + radius,    1.0f * static_cast<float>(M_PI) / 2.0f }, // Top-Left
	        { bounds.left + radius,  bounds.bottom - radius, 2.0f * static_cast<float>(M_PI) / 2.0f }, // Bottom-Left
	        { bounds.right - radius, bounds.bottom - radius, 3.0f * static_cast<float>(M_PI) / 2.0f }  // Bottom-Right
	    };
	
	    for (int i = 0; i < 4; ++i) {
	        for (int j = 0; j <= segments; ++j) {
	            float angle = corners[i].startAngle + (static_cast<float>(j) / static_cast<float>(segments)) * (static_cast<float>(M_PI) / 2.0f);
	            float vx = corners[i].x + std::cos(angle) * radius;
	            float vy = corners[i].y - std::sin(angle) * radius; // Inverted Y-axis tracking space
	            glVertex2f(vx, vy);
	        }
	    }
	    glEnd();
	}


	
	void DrawNativeSystemText(const char* text, float centerX, float baselineY) {
	    if (text == nullptr || text[0] == '\0') return;
	
	    int textWidth = 0;
	    int textHeight = 0;
	    
	    // Generate the bold, rounded-capsule texture matrix
	    HaikuTexture textTex = RenderWhiteTextToTexture(text, &textWidth, &textHeight, 11.0f);
	    if (textTex.id == 0) return;
	
	    // Direct mapping alignment bounding coordinates relative to icon centerline
	    float left = centerX - (textWidth / 2.0f);
	    float right = centerX + (textWidth / 2.0f);
	    float top = baselineY - textHeight;
	    float bottom = baselineY;
	
	    // State attribute protection sandbox
	    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT);
	    
	    glEnable(GL_TEXTURE_2D);
	    glBindTexture(GL_TEXTURE_2D, textTex.id);
	
	    glEnable(GL_BLEND);
	    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	    
	    // Keep internal color states tracking cleanly as absolute white
	    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
	
	    // Render the complete composite capsule quad asset onto the application layer
	    glBegin(GL_QUADS);
	        glTexCoord2f(0.0f, 0.0f); glVertex2f(left,  top);
	        glTexCoord2f(1.0f, 0.0f); glVertex2f(right, top);
	        glTexCoord2f(1.0f, 1.0f); glVertex2f(right, bottom);
	        glTexCoord2f(0.0f, 1.0f); glVertex2f(left,  bottom);
	    glEnd();
	
	    glPopAttrib();
	    glDeleteTextures(1, &textTex.id); // Eliminate dynamic memory leaks instantly
	}








void RenderFrame(float yOffset) {
	    // =========================================================================
	    // 1. STEP POSIX TIMING ENGINES AND KERNEL RECORD SAMPLES
	    // =========================================================================
	    SyncDockWithRunningDeskbarApps(); 
	    
	    UpdateLiveClockTexture();
	    UpdateGlobalCpuLoadTracker(); 
		static bigtime_t mouseLeftTime = 0; 
		bool mouseIsOverAnyIcon = false;   
		    
	    // KEEP THIS: You still need to read the live color from disk!
	    rgb_color systemBg = GetLiveSystemBackgroundColor();
	
	    // KEEP THIS: Keep updating your global class floats dynamically
	    fBgColorR = systemBg.red   / 255.0f;
	    fBgColorG = systemBg.green / 255.0f;
	    fBgColorB = systemBg.blue  / 255.0f;
	
	    // CHANGE THIS: Clear the canvas with transparency (0.0f alpha) 
	    // This allows your desktop wallpaper to show through properly!
	    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); 
	    glClear(GL_COLOR_BUFFER_BIT);
		
        // =========================================================================
        // 2. FULLSCREEN WALLPAPER DRAW PASS (ASPECT-ALIGNED) - STATIONARY
        // =========================================================================
        if (fWallpaperTexture.id != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, fWallpaperTexture.id);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

            // 1. Calculate aspect ratios
            float screenAspect = static_cast<float>(fWidth) / static_cast<float>(fHeight);
            float imageAspect  = static_cast<float>(fWallpaperTexture.width) / static_cast<float>(fWallpaperTexture.height);

            // Default to full texture boundaries
            float uMin = 0.0f, uMax = 1.0f;
            float vMin = 0.0f, vMax = 1.0f;

            // 2. Replicate Tracker's center-cropped scaling logic
            if (imageAspect > screenAspect) {
                // Image is wider than your screen aspect: crop left/right sides
                float cropWidth = static_cast<float>(fWallpaperTexture.height) * screenAspect;
                float horizontalDiff = (static_cast<float>(fWallpaperTexture.width) - cropWidth) / static_cast<float>(fWallpaperTexture.width);
                uMin = horizontalDiff / 2.0f;
                uMax = 1.0f - uMin;
            } else if (imageAspect < screenAspect) {
                // Image is taller than your screen aspect: crop top/bottom sides
                float cropHeight = static_cast<float>(fWallpaperTexture.width) / screenAspect;
                float verticalDiff = (static_cast<float>(fWallpaperTexture.height) - cropHeight) / static_cast<float>(fWallpaperTexture.height);
                vMin = verticalDiff / 2.0f;
                vMax = 1.0f - vMin;
            }

            // 3. Render utilizing your original upright orientation vertices
            glBegin(GL_QUADS);
                glTexCoord2f(uMin, vMin); glVertex2f(0.0f, 0.0f);
                glTexCoord2f(uMax, vMin); glVertex2f(static_cast<float>(fWidth), 0.0f);
                glTexCoord2f(uMax, vMax); glVertex2f(static_cast<float>(fWidth), static_cast<float>(fHeight));
                glTexCoord2f(uMin, vMax); glVertex2f(0.0f, static_cast<float>(fHeight));
            glEnd();

            glBindTexture(GL_TEXTURE_2D, 0); 
            glDisable(GL_TEXTURE_2D);
        }
			
        // =========================================================================
        // NEW MATRIX TRANSLATION FOR THE AUTOHIDE OVERLAY ELEMENTS
        // =========================================================================
        glPushMatrix();
        glTranslatef(0.0f, yOffset, 0.0f);

        // =========================================================================
        // 3. TASKBAR-ENABLED DOCK WIDTH GEOMETRY CALCULATIONS (UNIFIED ZOOM PIPELINE)
        // =========================================================================
        // FIXED SIZING PIPELINE: Replaced the hardcoded '48.0f' float limits completely 
        // with your live fBaseIconSize configuration setting variable!
        float baseSize = fBaseIconSize;
        float padding  = 16.0f;
        
        size_t baselineLaunchersCount = fDesktopItems.size() + 1; 

        size_t activeWindowsCount = 0;
        for (const auto& w : fTaskbarWindows) {
            if (*w.openStateFlag == true) activeWindowsCount++;
        }

        size_t totalIconsCount = baselineLaunchersCount + activeWindowsCount;

        // Configuration variables for the status widgets (Now scales dynamically relative to icon size changes!)
        float clockSectionPadding = 24.0f;
        float cpuGraphWidth       = 60.0f;
        float separatorGapPadding = 16.0f;        
        
        // FIXED SIZING: The trash bin launcher now scales uniformly alongside your application icons
        float baseTrashSize       = fBaseIconSize; 
        float baseVolumeWidth     = 44.0f; // Slider horizontal layout width footprint allocation

        // Arrays to store real-time calculations for EVERY component
        std::vector<float> dynamicWidths;
        std::vector<float> dynamicScales;
        float maxDockHeight = baseSize;

        // -------------------------------------------------------------------------
        // PASS 1: PROGRESSIVE MULTI-PASS COORDINATE RE-ANCHORING
        // -------------------------------------------------------------------------

        float totalCalculatedWidth = 0.0f;        
        for (int convergencePass = 0; convergencePass < 3; ++convergencePass) {
            dynamicWidths.clear();
            dynamicScales.clear();
            maxDockHeight = baseSize;
            
            // Start reading layouts from a relative left offset margin
            float progressiveX = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
            
            // 1. Process standard app launchers & active window indicators (2D SMOOTH FIX)
            for (size_t i = 0; i < totalIconsCount; ++i) {
                float approxCenterX = progressiveX + (baseSize / 2.0f);
                
                // Calculate the visual center point of the icon on the Y axis
                float approxCenterY = fHeight - 10.0f - (baseSize / 2.0f);
                
                // Compute independent delta vectors
                float distanceX = std::abs(fMouseX - approxCenterX);
                float distanceY = std::abs(fMouseY - approxCenterY);
                
                // Calculate true 2D hypotenuse distance from the mouse to the center of the icon
                float distance2D = std::sqrt(distanceX * distanceX + distanceY * distanceY);
                
                float scale = 1.0f;
                // FIX: Base magnification on the total 2D distance sphere (180.0f radius provides excellent glide feel)
                if (fCursorIsInsideHitbox && distance2D < 180.0f) {
                    float ratio = distance2D / 180.0f;
                    
                    // Smooth Gaussian bell-curve falloff transitions perfectly in all directions
                    scale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
                }

                float finalSize = baseSize * scale;
                dynamicWidths.push_back(finalSize);
                dynamicScales.push_back(scale);
                
                if (finalSize > maxDockHeight) maxDockHeight = finalSize;
                progressiveX += finalSize + padding;
            }


            
            if (totalIconsCount > 0) progressiveX -= padding; 

            // Account for the structural native app split divider
            if (activeWindowsCount > 0) {
                progressiveX += separatorGapPadding;
            }

            
            // =========================================================================
            // PROCESS HAIKU TRASH CAN COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            progressiveX += clockSectionPadding;
            float approxTrashCenterX = progressiveX + (baseTrashSize / 2.0f);
            
            // Calculate the spatial center point of the Trash Can icon on the Y axis
            float approxTrashCenterY = fHeight - 10.0f - (baseTrashSize / 2.0f);
            
            // Compute separate directional delta vectors
            float distanceTrashX = std::abs(fMouseX - approxTrashCenterX);
            float distanceTrashY = std::abs(fMouseY - approxTrashCenterY);
            
            // Calculate true 2D distance using the hypotenuse formula
            float distanceTrash2D = std::sqrt(distanceTrashX * distanceTrashX + distanceTrashY * distanceTrashY);
            
            float trashScale = 1.0f;
            // FIX: Rely purely on the 2D radial distance sphere (matching your 180.0f radius baseline)
            if (fCursorIsInsideHitbox && distanceTrash2D < 180.0f) {
                float ratio = distanceTrash2D / 180.0f;
                
                // Smooth Gaussian bell-curve falloff transitions cleanly in all 360 degrees
                trashScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            float finalTrashSize = baseTrashSize * trashScale;
            dynamicWidths.push_back(finalTrashSize);
            dynamicScales.push_back(trashScale);
            if (finalTrashSize > maxDockHeight) maxDockHeight = finalTrashSize;
            progressiveX += finalTrashSize;

 			
	   	    // =========================================================================
	        // DYNAMIC SYSTEM TRAY SLOT WIDTH PARAMETER (2D SMOOTH FIX)
	        // NOTE: Uses 6.0f internal spacing to match your main RenderFrame pipeline!
	        // =========================================================================
	        if (showSystemTray) {
            	// Run our throttled texture sync check
            	SyncDynamicSystrayTextures();
			}
	        float traySectionPadding = clockSectionPadding;
	        size_t trayCount = fLiveTrayItems.size();
	        
            float baselineTrayWidth = 0.0f;
            if (showSystemTray && trayCount > 0) {
                baselineTrayWidth = (trayCount * 16.0f) + ((trayCount > 1 ? trayCount - 1 : 0) * 6.0f);
            }
	
	        progressiveX += traySectionPadding;
	        float approxTrayCenterX = progressiveX + (baselineTrayWidth / 2.0f);
	        
	        // Calculate the standard spatial center point on the Y axis for the tray row
	        float approxTrayCenterY = fHeight - 10.0f - (baseSize / 2.0f);
	        
	        // Compute independent delta vectors
	        float distanceTrayX = std::abs(fMouseX - approxTrayCenterX);
	        float distanceTrayY = std::abs(fMouseY - approxTrayCenterY);
	        
	        // Calculate true 2D distance using the hypotenuse formula
	        float distanceTray2D = std::sqrt(distanceTrayX * distanceTrayX + distanceTrayY * distanceTrayY);
	        
	        float trayScale = 1.0f;
	        // FIX: Base magnification entirely on the 2D radial distance sphere
	        if (fCursorIsInsideHitbox && distanceTray2D < 180.0f) {
	            float ratio = distanceTray2D / 180.0f;
	            
	            // Smooth Gaussian bell-curve falloff transitions cleanly in all directions
	            trayScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        
	        dynamicWidths.push_back(baselineTrayWidth * trayScale);
	        dynamicScales.push_back(trayScale);
	        progressiveX += (baselineTrayWidth * trayScale);
	        // =========================================================================

 			
            // =========================================================================
            // PROCESS SYSTEM CLOCK COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            if (fClockTexture.id != 0) {
                progressiveX += clockSectionPadding;
                
                float highDpiCompensateFactor = 0.42f;
                float baselineClockLayoutWidth = static_cast<float>(fClockWidth) * highDpiCompensateFactor;
                
                float approxClockCenterX = progressiveX + (baselineClockLayoutWidth / 2.0f);
                
                // Calculate spatial center point on the Y axis for the text string element
                float approxClockCenterY = fHeight - 10.0f - (baseSize / 2.0f);
                
                float distanceClockX = std::abs(fMouseX - approxClockCenterX);
                float distanceClockY = std::abs(fMouseY - approxClockCenterY);
                float distanceClock2D = std::sqrt(distanceClockX * distanceClockX + distanceClockY * distanceClockY);
                
                float clockScale = 1.0f;
                // FIX: Base magnification entirely on the unified 180.0f radial distance circle
                if (fCursorIsInsideHitbox && distanceClock2D < 180.0f) {
                    float ratio = distanceClock2D / 180.0f;
                    clockScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
                }
                
                dynamicWidths.push_back(baselineClockLayoutWidth * clockScale);
                dynamicScales.push_back(clockScale);
                progressiveX += (baselineClockLayoutWidth * clockScale);
            } else {
                dynamicWidths.push_back(0.0f);
                dynamicScales.push_back(1.0f);
            }

            // =========================================================================
            // PROCESS DYNAMIC VOLUME SLIDER COMPONENT METRICS (2D SMOOTH FIX)
            // =========================================================================
            float layoutSizeRatio = baseSize / 48.0f;
            
            // Proportional Separator
            progressiveX += (clockSectionPadding * layoutSizeRatio);
            
            float scaledBaseVolumeWidth = baseVolumeWidth * layoutSizeRatio;
            float approxVolCenterX = progressiveX + (scaledBaseVolumeWidth / 2.0f);
            float approxVolCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceVolX = std::abs(fMouseX - approxVolCenterX);
            float distanceVolY = std::abs(fMouseY - approxVolCenterY);
            float distanceVol2D = std::sqrt(distanceVolX * distanceVolX + distanceVolY * distanceVolY);
            
            float volScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceVol2D < 180.0f) {
                float ratio = distanceVol2D / 180.0f;
                volScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            dynamicWidths.push_back(scaledBaseVolumeWidth * volScale);
            dynamicScales.push_back(volScale);
            progressiveX += (scaledBaseVolumeWidth * volScale);

            // =========================================================================
            // PROCESS GRAPHICAL CPU MONITOR METRICS (2D SMOOTH FIX)
            // =========================================================================
            // Proportional Separator
            progressiveX += (clockSectionPadding * layoutSizeRatio); 
            
            float scaledCpuGraphWidth = cpuGraphWidth * layoutSizeRatio;
            float approxCpuCenterX = progressiveX + (scaledCpuGraphWidth / 2.0f);
            float approxCpuCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceCpuX = std::abs(fMouseX - approxCpuCenterX);
            float distanceCpuY = std::abs(fMouseY - approxCpuCenterY);
            float distanceCpu2D = std::sqrt(distanceCpuX * distanceCpuX + distanceCpuY * distanceCpuY);
            
            float cpuScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceCpu2D < 180.0f) {
                float ratio = distanceCpu2D / 180.0f;
                cpuScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            float finalCpuWidth = scaledCpuGraphWidth * cpuScale;
            dynamicWidths.push_back(finalCpuWidth);
            dynamicScales.push_back(cpuScale);
            progressiveX += finalCpuWidth;

            float leftEdge = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
            totalCalculatedWidth = progressiveX - leftEdge;
        }





		//@here0
        //Renderdraw
        // -----------------------------	--------------------------------------------
        // PASS 2: BOUNDS SETTLEMENT AND BACKPLATE GEOMETRY ALLOCATION
        // -------------------------------------------------------------------------
        size_t trashSlotIdx  = totalIconsCount;
        size_t traySlotIdx   = totalIconsCount + 1;
        size_t clockSlotIdx  = totalIconsCount + 2;
        size_t volumeSlotIdx = totalIconsCount + 3;
        size_t cpuSlotIdx    = totalIconsCount + 4;
        
        // Calculate our dynamic size ratio multiplier based on your slider baseline
        float layoutSizeRatio = baseSize / 48.0f;
		float leftPaddingbuffer   = 1.0f;
        // --- RE-ADDED MISSING VARIABLE DECLARATIONS ---
        float dockMarginBottom = 15.0f;
        HaikuRect dockPlate;
        // ----------------------------------------------		
        // SMART SIZING: Side padding changes dynamically with the icon size scale factor!
        float internalSidePadding = fBaseIconSize * layoutSizeRatio; 
        
        // SMART SCALED BUFFER: Calculates a safety buffer if layout drift occurs at high scales
        float clippingCompensation = 0.0f;
        if (layoutSizeRatio > 1.0f) {
            clippingCompensation = (leftPaddingbuffer * (layoutSizeRatio - 60.0f));
        }

        // Apply our responsive width tracking variable
        float adjustedTotalWidth = totalCalculatedWidth + clippingCompensation;

        // Balance the dock backplate relative to the dynamically scaled row footprint
        dockPlate.left   = (fWidth / 2.0f) - (adjustedTotalWidth / 2.0f) - internalSidePadding;
        dockPlate.right  = (fWidth / 2.0f) + (adjustedTotalWidth / 2.0f) + internalSidePadding;
        dockPlate.bottom = fHeight - dockMarginBottom;
        
        // DYNAMIC HEIGHT SCALE: Automatically adapts panel thickness to the icons
        dockPlate.top    = dockPlate.bottom - maxDockHeight - 20.0f;

        // Lock down definitive trash hitbox using converged data fields
        float renderingTrashSize = dynamicWidths[trashSlotIdx];

        fTrashRect.right = fTrashRect.left + renderingTrashSize;
        fTrashRect.top = dockPlate.bottom - 10.0f - renderingTrashSize;
        fTrashRect.bottom = dockPlate.bottom - 10.0f;

     
        // =========================================================================
        // BACKPLATE CONTAINER SHELF RENDERING (OPTION A: DYNAMIC SYNCD FADE)
        // =========================================================================
        glEnable(GL_BLEND); 
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        float cornerRadius = 15.0f;

        //DrawFilledRoundedRect(dockPlate, cornerRadius, 0.95f, 0.95f, 0.95f, fDockAlpha); 

        DrawFilledRoundedRect(dockPlate, cornerRadius, fBgColorR, fBgColorG, fBgColorB, fDockAlpha); 
        
        DrawOutlineRoundedRect(dockPlate, cornerRadius, 0.15f, 0.15f, 0.15f, fDockAlpha); 

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        // =========================================================================
        // 5. CORE ICON RENDERING ENGINE WITH ACTIVE RUNNING TASKBAR SPLITS
        // =========================================================================
        // -------------------------------------------------------------------------
        // DEFERRED HOVER STATE REGISTERS (Clears state for the fresh rendering pass)
        // -------------------------------------------------------------------------
        

        BString activeDisplayString = "";
        
        // -------------------------------------------------------------------------
        // VERTICAL PREVIEW LIST SETUP
        // -------------------------------------------------------------------------
        fShouldDrawList = false;
        float listBaseX = 0.0f;
        float listBaseY = 0.0f;


        float currentX = dockPlate.left + 20.0f;
        size_t renderingSlotIdx = 0;

		// Step A
        // STEP 1: RENDER THE BASELINE SYSTEM SHORTCUTS (LEAF MENU + DESKTOP DIRECTORY ENTRIES)
        for (size_t i = 0; i < baselineLaunchersCount; ++i) {
            float size = dynamicWidths[renderingSlotIdx];
            float scale = dynamicScales[renderingSlotIdx];
            HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };

            if (i == 0) {
                if (fHaikuMenuIcon.id != 0) {
                    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fHaikuMenuIcon.id);
                    glBegin(GL_QUADS);
                        glTexCoord2f(0.0f, 0.0f); glVertex2f(iconBounds.left, iconBounds.top);
                        glTexCoord2f(1.0f, 0.0f); glVertex2f(iconBounds.right, iconBounds.top);
                        glTexCoord2f(1.0f, 1.0f); glVertex2f(iconBounds.right, iconBounds.bottom);
                        glTexCoord2f(0.0f, 1.0f); glVertex2f(iconBounds.left, iconBounds.bottom);
                    glEnd();
                    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                }
            } else {
                size_t itemIdx = i - 1; auto& item = fDesktopItems[itemIdx];
                if (item.texture.id != 0) {
                    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, item.texture.id);
                    glBegin(GL_QUADS);
                        glTexCoord2f(0.0f, 0.0f); glVertex2f(iconBounds.left, iconBounds.top);
                        glTexCoord2f(1.0f, 0.0f); glVertex2f(iconBounds.right, iconBounds.top);
                        glTexCoord2f(1.0f, 1.0f); glVertex2f(iconBounds.right, iconBounds.bottom);
                        glTexCoord2f(0.0f, 1.0f); glVertex2f(iconBounds.left, iconBounds.bottom);
                    glEnd();
                    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                }
                if (scale > 1.4f && item.textTexture.id != 0) {
                    int tw = 0, th = 0;
                    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, item.textTexture.id);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
                    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Solid crisp white tooltip text ink
                    float tx = currentX + (size / 2.0f) - (tw / 2.0f); float ty = iconBounds.top - th - 6.0f;
                    glBegin(GL_QUADS);
                        glTexCoord2f(0.0f, 0.0f); glVertex2f(tx, ty); glTexCoord2f(1.0f, 0.0f); glVertex2f(tx + tw, ty);
                        glTexCoord2f(1.0f, 1.0f); glVertex2f(tx + tw, ty + th); glTexCoord2f(0.0f, 1.0f); glVertex2f(tx, ty + th);
                    glEnd();
                    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                }
            }
            currentX += size + padding;
            renderingSlotIdx++;
        }

		// STEP 2: IF RUNNING WINDOWS EXIST, DRAW A STRUCTURAL TASKBAR VERTICAL DIVIDER LINE
		if (activeWindowsCount > 0) {
		    currentX += (separatorGapPadding / 2.0f) - padding;
		    
				// DIVIDER LINE 
		        float lineLeftSnappedX = std::floor(currentX + 0.5f);
		        glLineWidth(2.0f); 
		        glColor4f(0.15f, 0.15f, 0.15f, fDockAlpha * 0.5f); 		        
		        glBegin(GL_LINES);
		            glVertex2f(lineLeftSnappedX, dockPlate.top + 8.0f);
		            glVertex2f(lineLeftSnappedX, dockPlate.bottom - 8.0f);
		        glEnd();		        
		        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
					    
			currentX += (separatorGapPadding / 2.0f) + padding;
			}

		
		// =========================================================================
		// STEP 3: RENDER THE LIVE RUNNING APPLICATION WINDOW CONTEXT TAIL LOG MODULES
		// =========================================================================
		
		// PERFORMANCE ANCHOR: Fetch the active workspace and system window tokens ONCE 
		// before drawing. This reduces CPU utilization to virtually zero during redrawing.
		int32 currentWorkspace = current_workspace();
		int32* windowTokens = nullptr;
		int32 totalWindows = 0;
		BPrivate::get_window_order(currentWorkspace, &windowTokens, &totalWindows);
	
		for (size_t w = 0; w < fTaskbarWindows.size(); ++w) {
		    auto& activeTaskWin = fTaskbarWindows[w];		
		    float size = dynamicWidths[renderingSlotIdx];
		    HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
		
		    bool isTracker = (activeTaskWin.title == "Tracker");
		    int32 normalVisibleWindows = 0;
		    int32 totalTeamWindows = 0;
	
		    // 1. UNIVERSAL STATE CHECKING PIPELINE (Replaces thread counters and streaks)
		    if (windowTokens != nullptr && totalWindows > 0) {
		        for (int32 i = 0; i < totalWindows; ++i) {
		            client_window_info* info = get_window_info(windowTokens[i]);
		            if (info == nullptr) continue;
	
		            if (info->team == activeTaskWin.teamId) {
		                if (isTracker) {
		                    // TRACKER RULE: Target only active folder panels (layer 3+) 
		                    // Discards desktop backdrop (1024) and right-click popups (1025)
		                    if (info->feel == B_NORMAL_WINDOW_FEEL && info->layer >= 3) {
		                        totalTeamWindows++;
		                        if (!info->is_mini && (info->workspaces & (1 << currentWorkspace))) {
		                            normalVisibleWindows++;
		                        }
		                    }
		                } else {
		                    // STANDARD APPLICATIONS RULE: Filter by standard user-facing window behaviors
		                    if (info->feel == B_NORMAL_WINDOW_FEEL) {
		                        totalTeamWindows++;
		                        if (!info->is_mini && info->layer > 0) {
		                            if (info->workspaces & (1 << currentWorkspace)) {
		                                normalVisibleWindows++;
		                            }
		                        }
		                    }
		                }
		            }
		            free(info);
		        }
		    }
	
		    // Deduce if the application's graphical canvas is completely folded down
		    bool appIsGenuinelyMinimized = false;
		    if (isTracker) {
		        appIsGenuinelyMinimized = (normalVisibleWindows == 0);
		    } else if (totalTeamWindows > 0) {
		        appIsGenuinelyMinimized = (normalVisibleWindows == 0);
		    }
	
		    // 2. NATIVE FOREGROUND FOCUS CHECKING
		    app_info activeAppInfo;
		    bool isCurrentlyForeground = false;
	
		    if (be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
		        if (activeAppInfo.team == activeTaskWin.teamId) {
		            isCurrentlyForeground = true;
		            
		            // If Tracker is currently focused but has no active folder windows open,
		            // it is definitively a desktop background click. Treat it as minimized.
		            if (isTracker && normalVisibleWindows == 0) {
		                isCurrentlyForeground = false;
		                appIsGenuinelyMinimized = true;
		            }
		        }
		    }
	
		    // 3. ZERO-LAG ASSIGNMENT 
		    if (isCurrentlyForeground) {
		        activeTaskWin.isMinimized = false;
		    } else {
		        activeTaskWin.isMinimized = appIsGenuinelyMinimized;
		    }
		    
	    // =========================================================================
	    // STEP 4: DRAW WINDOW ICON THUMBNAIL CORES AND ACTIVE INDICATORS
	    // =========================================================================
	
	    // A. Draw active task window application vector icon thumbnail
	    if (activeTaskWin.icon.id != 0) {
	        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, activeTaskWin.icon.id);
	        if (activeTaskWin.isMinimized == true) {
	            glColor4f(1.0f, 1.0f, 1.0f, 0.45f); // 45% opacity soft focus ghosting
	        } else {
	            glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Bright full opacity active focus
	        }
	
	        glBegin(GL_QUADS);
	            glTexCoord2f(0.0f, 0.0f); glVertex2f(iconBounds.left,  iconBounds.top);
	            glTexCoord2f(1.0f, 0.0f); glVertex2f(iconBounds.right, iconBounds.top);
	            glTexCoord2f(1.0f, 1.0f); glVertex2f(iconBounds.right, iconBounds.bottom);
	            glTexCoord2f(0.0f, 1.0f); glVertex2f(iconBounds.left,  iconBounds.bottom);
	        glEnd();
	        glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
	    }
	    

			//@here titles
			
		    // =========================================================================
		    // HOVER TITLE SYSTEM TEXT OVERLAY (Inside the loop)
		    // =========================================================================
		    const float BASE_ICON_SIZE_THRESHOLD = 48.0f; 
		    if (size > BASE_ICON_SIZE_THRESHOLD && fShowTitleOverlays) {
		        
		        // Check if the current layout window match represents the item under active hover
		        // FIX: Allow fMouseY to go ABOVE the icon into the text overlay area (e.g., 40 pixels higher)
		        if (fMouseX >= iconBounds.left && fMouseX <= iconBounds.right &&
		            fMouseY >= (iconBounds.top - 40.0f) && fMouseY <= iconBounds.bottom) {
		            
		            listBaseX = iconBounds.left + ((iconBounds.right - iconBounds.left) / 2.0f);
		            listBaseY = iconBounds.top - 12.0f;
		            
		            if (fHoveredTeam != activeTaskWin.teamId) {
		                fHoveredTeam = activeTaskWin.teamId;
		                GetTrackedWindowsFromTeam(fHoveredTeam, fCurrentWindowsList);
		            }
		            
		            fShouldDrawList = true;
		            mouseIsOverAnyIcon = true; 
		            mouseLeftTime = 0;         
		        }
		    }
		    
		    currentX += size + padding;
		    renderingSlotIdx++;
		} 

	
	    // 4. RESOURCE CLEANUP PAIRING
	    if (windowTokens != nullptr) {
	        free(windowTokens);
	    }
	
	    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Reset texture filters cleanly
	
	    // =========================================================================
	    // NO-HOVER DETECTOR
	    // =========================================================================
	    // If the loop finished and the mouse wasn't over any icon, trigger the countdown
	    if (!mouseIsOverAnyIcon && fShouldDrawList) {
	        if (mouseLeftTime == 0) {
	            mouseLeftTime = system_time(); // Mark the microsecond timestamp when the mouse left
	        }
	        
	        // Check if 2 seconds (2,000,000 microseconds) have passed
	        if ((system_time() - mouseLeftTime) >= 2000000) {
	            fShouldDrawList = false; // This turns off the rendering pipeline block cleanly
	            mouseLeftTime = 0;
	        }
	    }

	
        // =========================================================================
        // LEFT CLOCK DIVIDER LINE 
        // =========================================================================
        float lineLeftSnappedX = std::floor(currentX + 0.5f);
        glLineWidth(2.0f); 
        
        // FIXED HYBRID SCALING: We multiply fDockAlpha by 0.5f. 
        // This caps the divider's maximum opacity to a beautifully soft 50%,
        // but drops all the way down to a clean 0.0f if full transparency is activated!
        glColor4f(0.15f, 0.15f, 0.15f, fDockAlpha * 0.5f); 
        
        glBegin(GL_LINES);
            glVertex2f(lineLeftSnappedX, dockPlate.top + 8.0f);
            glVertex2f(lineLeftSnappedX, dockPlate.bottom - 8.0f);
        glEnd();
        
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	       
	    // =========================================================================
        // 6C. DRAW HAIKU TRASH BIN
        // =========================================================================
        uint32 currentTicks = SDL_GetTicks();
        if (currentTicks - fLastTrashCheckTime > 500) { 
            fLastTrashCheckTime = currentTicks;
            if (fHaikuTrashIcon.id != 0) {
                glDeleteTextures(1, &fHaikuTrashIcon.id);
                fHaikuTrashIcon.id = 0;
            }
            fHaikuTrashIcon = LoadIconFromNode("/boot/trash", 128);
        }
		
        // Extra divider lines cleanly neutralized to maintain your preferred borderless style
        currentX += clockSectionPadding;
		
        // Pin hitbox geometry directly to our current track pointer
        fTrashRect.left   = currentX;
        fTrashRect.right  = fTrashRect.left + renderingTrashSize;
        fTrashRect.top    = dockPlate.bottom - 10.0f - renderingTrashSize;
        fTrashRect.bottom = dockPlate.bottom - 10.0f;

        if (fHaikuTrashIcon.id != 0) {
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fHaikuTrashIcon.id);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(fTrashRect.left,  fTrashRect.top);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(fTrashRect.right, fTrashRect.top);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(fTrashRect.right, fTrashRect.bottom);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(fTrashRect.left,  fTrashRect.bottom);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
        }

        currentX = fTrashRect.right;   
	       
	       

        // =========================================================================
        // NATIVE INTEGRATION: DRAW THE SYSTEM TRAY (DYNAMIC DOCK ENGINE REWRITE)
        // =========================================================================
        if (showSystemTray) {
	        float dynamicTrayWidth = dynamicWidths[traySlotIdx];
	        float trayScaleFactor  = dynamicScales[traySlotIdx];
	        
	        currentX += clockSectionPadding;
	        
	        // Vertically align the 16px high tray block cleanly within the panel
	        float trayRenderTopY = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (8.0f * trayScaleFactor));
	        
	        float localTrayX = currentX;
	        float traySpacing = 6.0f * trayScaleFactor;
	
	        // Force a completely clean OpenGL texturing environment state block
	        glEnable(GL_BLEND);
	        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	        glEnable(GL_TEXTURE_2D);
	        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	
	        // Iterate through all live elements dynamically extracted by the background engine
	        for (size_t i = 0; i < fLiveTrayItems.size(); ++i) {
	            GLuint trayTexID = fLiveTrayItems[i].textureId;
	            if (trayTexID == 0) continue;
	
	            float itemWidth = 16.0f * trayScaleFactor;
	
	            // =========================================================================
	            // CACHE RENDER POSITION METRICS FOR FAST PASS-2 HIT TESTING IN MOUSE CLICK
	            // =========================================================================
	            fLiveTrayItems[i].currentRenderX = localTrayX;
	            fLiveTrayItems[i].currentRenderWidth = itemWidth;
	
	            // Bind explicitly for this specific quad draw task run
	            glBindTexture(GL_TEXTURE_2D, trayTexID);
	            glBegin(GL_QUADS);
	                glTexCoord2f(0.0f, 0.0f); glVertex2f(localTrayX, trayRenderTopY);
	                glTexCoord2f(1.0f, 0.0f); glVertex2f(localTrayX + itemWidth, trayRenderTopY);
	                glTexCoord2f(1.0f, 1.0f); glVertex2f(localTrayX + itemWidth, trayRenderTopY + itemWidth);
	                glTexCoord2f(0.0f, 1.0f); glVertex2f(localTrayX, trayRenderTopY + itemWidth);
	            glEnd();
	
	            // Advance layout vector forward using dynamic scaling specs
	            localTrayX += itemWidth + traySpacing;
	        }
			/*
	        // --- CRITICAL DEFENSIVE SHIELD: FORCE FULL OpenGL STATE SHUTDOWN ---
	        // This explicitly cuts off the texture matrix pipeline, guaranteeing the clock text 
	        // and trash bin drawing routines downstream inherit a pristine state machine!
	        glBindTexture(GL_TEXTURE_2D, 0);
	        glDisable(GL_TEXTURE_2D);
	        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	        glColor4f(fBgColorR, fBgColorG, fBgColorB, 1.0f);
	        // =========================================================================
			*/
	        currentX += dynamicTrayWidth;
        }





        // =========================================================================
        // 6. DRAW SYSTEM CLOCK STATUS TEXT (PROPORTIONAL SIZE SCALING)
        // =========================================================================
        if (fClockTexture.id != 0) {

            float clockScale = dynamicScales[clockSlotIdx];
            
            // DYNAMIC SIZING: Calculate scaling ratio relative to our base 48.0f profile
            float sizeRatio = baseSize / 48.0f;
            
            // Dynamically scales text width and texture height relative to the slider settings
            float dynamicClockW = dynamicWidths[clockSlotIdx] * sizeRatio; 
            float highDpiCompensateFactor = 0.42f; 
            float dynamicClockH = static_cast<float>(fClockHeight) * highDpiCompensateFactor * clockScale * sizeRatio;

            currentX += clockSectionPadding;
            float clockY = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicClockH / 2.0f));
            
            HaikuRect clockB = { 
                std::floor(currentX + 0.5f), 
                std::floor(clockY + 0.5f), 
                std::floor(currentX + dynamicClockW + 0.5f), 
                std::floor(clockY + dynamicClockH + 0.5f) 
            };
            
            // Hover date detection boundaries scale automatically too!
            bool isMouseHoveringClock = (fMouseX >= clockB.left && fMouseX <= clockB.right &&
                                         fMouseY >= clockB.top  && fMouseY <= clockB.bottom);
            
            if (isMouseHoveringClock) {
                time_t rawTime = time(nullptr);
                struct tm* timeInfo = localtime(&rawTime);
                
                int day = timeInfo->tm_mday;
                BString suffix = "th";
                if (day == 1 || day == 21 || day == 31) suffix = "st";
                else if (day == 2 || day == 22) suffix = "nd";
                else if (day == 3 || day == 23) suffix = "rd";
                
                char dateBuffer[32];
                strftime(dateBuffer, sizeof(dateBuffer), "%b ", timeInfo); 
                
                BString dateStr;
                dateStr << dateBuffer << day << suffix << " " << (timeInfo->tm_year + 1900);
                
                if (fDockAlpha < 0.35f) {
                    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                } else {
                    glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
                }

                float textCenterX = clockB.left + (dynamicClockW / 2.0f);
                // Offset the floating date height proportionally to stay clear of the larger text
                DrawNativeSystemText(dateStr.String(), textCenterX, clockY - (16.0f * sizeRatio));
            }

            // Draw standard time texture matching your high-contrast logic
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fClockTexture.id);
            
            if (fDockAlpha < 0.35f) {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
            } else {
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
            }
            
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(clockB.left, clockB.top);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(clockB.right, clockB.top);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(clockB.right, clockB.bottom);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(clockB.left, clockB.bottom);
            glEnd();
            
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
            
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
            currentX += dynamicClockW;
        }




        // =========================================================================
        // NEW: DRAW DYNAMIC VOLUME CONTROL SLIDER (DYNAMIC SIZE SCALING & SOLID BLACK)
        // =========================================================================
        FetchHaikuMixerVolume(); 
        
        currentX += clockSectionPadding;
        
        float volScale = dynamicScales[volumeSlotIdx];
        
        // FIXED TYPE RESOLUTION: Declared as an independent local 'float volSizeRatio' variable 
        // to bypass any surrounding variable declaration scope conflicts completely!
        float volSizeRatio = baseSize / 48.0f;
        
        // Dynamically scale width and height relative to the slider settings
        float dynamicVolWidth   = dynamicWidths[volumeSlotIdx] * volSizeRatio;
        float dynamicVolHeight  = 12.0f * volScale * volSizeRatio; 
        float volTop = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicVolHeight / 2.0f));
        
        // CACHE PIPELINE: Store the updated, scaled bounds so hit-testing inputs align perfectly
        fCachedVolLeft   = currentX;
        fCachedVolTop    = volTop;
        fCachedVolWidth  = dynamicVolWidth;
        fCachedVolHeight = dynamicVolHeight;

        HaikuRect volBounds = { currentX, volTop, currentX + dynamicVolWidth, volTop + dynamicVolHeight };

        // 1. RESTORED SOLID BACKGROUND: Swapped 0.9f out for a locked 0.95f dark matte casing trough.
        DrawFilledRect(volBounds, 0.03f, 0.05f, 0.03f, 0.95f); 
        
        // 2. Draw active volume fill level (Green)
        HaikuRect activeVolumeFill = {
            volBounds.left,
            volBounds.top,
            volBounds.left + (dynamicVolWidth * fCurrentVolumeLevel),
            volBounds.bottom
        };
        DrawFilledRect(activeVolumeFill, 0.2f, 1.0f, 0.2f, 0.85f);

        // 3. Draw a thin perimeter frame outline edge loop (Soften its opacity to match the theme)
        glColor4f(0.15f, 0.15f, 0.15f, fDockAlpha * 0.5f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(volBounds.left,  volBounds.top);    glVertex2f(volBounds.right, volBounds.top);
            glVertex2f(volBounds.right, volBounds.bottom); glVertex2f(volBounds.left,  volBounds.bottom);
        glEnd();

        // Restore universal color state safety pass
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        currentX += dynamicVolWidth;



        // =========================================================================
        // 6B. DRAW GRAPHICAL PURPLE BOUNCING CPU METERS (DYNAMIC SIZING & SOLID BLACK)
        // =========================================================================
        glLineWidth(2.0f);        
        currentX += clockSectionPadding;        
        
        float cpuScale = dynamicScales[cpuSlotIdx];
        
        // DYNAMIC SIZING: Calculate a scale ratio relative to your base 48.0f icon setting
        float sizeRatio = baseSize / 48.0f;
        
        // Scales the width and height parameters proportionally as you move the slider
        float dynamicGraphWidth  = dynamicWidths[cpuSlotIdx];
        float dynamicGraphHeight = 28.0f * cpuScale * sizeRatio; 
        
        float graphTop = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicGraphHeight / 2.0f));
        HaikuRect cpuGraphBounds = { currentX, graphTop, currentX + dynamicGraphWidth, graphTop + dynamicGraphHeight };

        // RESTORED DARK BLACK BACKGROUND: Opacity locked back to a rich 95% dark charcoal capsule,
        // ensuring the purple bars pop with maximum contrast even over 100% clear backplates!
        DrawGLRoundedRect(cpuGraphBounds, 4.0f, 0.03f, 0.03f, 0.05f, 0.95f, true); 
        
        UpdateGlobalCpuLoadTracker();

        int numBars = (fCpuHistoryIndex > 0 && fCpuHistoryIndex <= 40) ? fCpuHistoryIndex : 16;
        float barSpacing = 1.5f * sizeRatio; // Scale bar spacing proportionally
        float totalSpacingSpace = barSpacing * (numBars + 1);
        float barWidth = (dynamicGraphWidth - totalSpacingSpace) / numBars;

        static std::vector<float> visualBouncingHeights(40, 0.0f);

        glBegin(GL_QUADS);
        for (int i = 0; i < numBars; ++i) {
            float targetLoadFactor = fCpuHistory[i];
            visualBouncingHeights[i] = (visualBouncingHeights[i] * 0.82f) + (targetLoadFactor * 0.18f);

            float barLeft = cpuGraphBounds.left + barSpacing + (i * (barWidth + barSpacing));
            float barRight = barLeft + barWidth;
            
            float barTop = cpuGraphBounds.bottom - (visualBouncingHeights[i] * (dynamicGraphHeight - 2.0f)) - 1.0f;
            
            // Dynamic Contrast: keep them bright and vivid
            if (fDockAlpha < 0.35f) {
                glColor4f(0.68f, 0.25f, 1.00f, 0.95f); // Bright luminous purple
            } else {
                glColor4f(0.57f, 0.12f, 0.99f, 0.90f); // Default Neon Purple
            }
            
            glVertex2f(barLeft,  barTop);
            glVertex2f(barRight, barTop);
            glVertex2f(barRight, cpuGraphBounds.bottom - 1.0f);
            glVertex2f(barLeft,  cpuGraphBounds.bottom - 1.0f);
        }
        glEnd();

        // Restore global color state sanity
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        
        currentX += dynamicGraphWidth;


        // =========================================================================
        // ADDED: HOVER PROXIMITY TEST AND DYNAMIC PERCENTAGE TEXT LAYER
        // =========================================================================

        if (cpuGraphBounds.Contains(fMouseX, fMouseY)) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            int latestIndex = (fCpuHistoryIndex == 0) ? 39 : fCpuHistoryIndex - 1;
            int cpuPercent = static_cast<int>(fCpuHistory[latestIndex] * 100.0f);

            char textBuffer[32]; snprintf(textBuffer, sizeof(textBuffer), "CPU: %d%%", cpuPercent);
            std::string currentTooltipStr(textBuffer);

            if (currentTooltipStr != fLastCpuTooltipStr) { 
                if (fCpuTooltipTex.id != 0) { 
                    glDeleteTextures(1, &fCpuTooltipTex.id); 
                    fCpuTooltipTex.id = 0; 
                } 
                fLastCpuTooltipStr = currentTooltipStr;
                fCpuTooltipTex = RenderTextToTexture(fLastCpuTooltipStr.c_str(), &fCpuTooltipW, &fCpuTooltipH);
            }                                 

            float tooltipW = static_cast<float>(fCpuTooltipW) + 12.0f; 
            float tooltipH = static_cast<float>(fCpuTooltipH) + 8.0f;
            float tooltipLeft = cpuGraphBounds.left + (cpuGraphBounds.Width() / 2.0f) - (tooltipW / 2.0f);
            
            // FIX: Brought the box lower down closer to the graph frame edge (changed from -8.0f to -1.0f)
            float tooltipBottom = cpuGraphBounds.top - 1.0f; 

            HaikuRect tooltipBounds = { tooltipLeft, tooltipBottom - tooltipH, tooltipLeft + tooltipW, tooltipBottom };
            DrawFilledRect(tooltipBounds, 0.15f, 0.15f, 0.15f, 0.75f);
            
            glColor4f(0.10f, 0.10f, 0.10f, 0.5f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(tooltipBounds.left,  tooltipBounds.top);   glVertex2f(tooltipBounds.right, tooltipBounds.top);
                glVertex2f(tooltipBounds.right, tooltipBounds.bottom); glVertex2f(tooltipBounds.left,  tooltipBounds.bottom);
            glEnd();

            if (fCpuTooltipTex.id != 0) {
                glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fCpuTooltipTex.id);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PRIMARY_COLOR);
                glColor4f(0.2f, 1.0f, 0.2f, 1.0f);                
                float textX = tooltipBounds.left + 6.0f; float textY = tooltipBounds.top + 4.0f;
                
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textX, textY);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textX + fCpuTooltipW, textY);
                    // FIXED: Replaced fTrashTooltipH with fCpuTooltipH to fix empty initialization geometry layout bug
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textX + fCpuTooltipW, textY + fCpuTooltipH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textX, textY + fCpuTooltipH);
                glEnd();               

                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
            }
        }
        
        glDisable(GL_BLEND);


   
        // 4. Update the tracker pointer past the cpu monitor graph layout bounds area cleanly
        currentX += dynamicGraphWidth;
        currentX += clockSectionPadding;
        
        fLastCalculatedWidth = totalCalculatedWidth;

        // =========================================================================
        // DEFERRED RENDERING PASS: UNIFIED INTERACTIVE HOVER TITLE OVERLAY
        // =========================================================================
        if (fShouldDrawList && !fCurrentWindowsList.empty()) {
            BString displayTitle = fCurrentWindowsList[0].title;
            
            float textEstimatedWidth = 240.0f; 
            BRect unifiedBox;
            unifiedBox.left   = listBaseX - (textEstimatedWidth / 2.0f);
            unifiedBox.right  = listBaseX + (textEstimatedWidth / 2.0f);
            unifiedBox.top    = listBaseY - 14.0f;
            unifiedBox.bottom = listBaseY + 4.0f;

            // Tracking registers to compute the elapsed hover duration safely across draw frames
            static team_id lastCheckedTeam = -1;
            static bigtime_t hoverStartTime = 0;
            bigtime_t currentTime = system_time();

            if (fMouseX >= unifiedBox.left && fMouseX <= unifiedBox.right &&
                fMouseY >= unifiedBox.top  && fMouseY <= unifiedBox.bottom) {
                
                // 1. If this is the very first frame the mouse entered this box, start the stopwatch
                if (hoverStartTime == 0 || fHoveredTeam != lastCheckedTeam) {
                    hoverStartTime = currentTime;
                    lastCheckedTeam = fHoveredTeam;
                }

                // 2. Compute the difference. 0.75 seconds translates to exactly 750,000 microseconds.
                if (currentTime - hoverStartTime >= 750000) {
                    
                    // =========================================================================
                    // LOW-LEVEL HOVER FOCUS PIPELINE
                    // =========================================================================
                    app_info targetAppInfo;
                    if (be_roster->GetRunningAppInfo(fHoveredTeam, &targetAppInfo) == B_OK) {
                        be_roster->ActivateApp(targetAppInfo.team);
                    } else {
                        be_roster->ActivateApp(fHoveredTeam);
                    }
                    
                    // Flush low-level bring-to-front message straight to the app_server link
                    BPrivate::AppServerLink link;
                    link.StartMessage(AS_BRING_TEAM_TO_FRONT);
                    link.Attach<team_id>(fHoveredTeam);
                    link.Flush();
                    
                    // Wake up hidden/minimized windows belonging to this specific team thread context
                    int32 systemCount = 0;
                    int32 currentWorkspace = current_workspace();
                    int32* systemTokens = nullptr;
                    if (BPrivate::get_window_order(currentWorkspace, &systemTokens, &systemCount) == B_OK && systemTokens != nullptr) {
                        for (int32 i = 0; i < systemCount; ++i) {
                            client_window_info* cInfo = get_window_info(systemTokens[i]);
                            if (cInfo != nullptr) {
                                if (cInfo->team == fHoveredTeam && cInfo->feel == B_NORMAL_WINDOW_FEEL) {
                                    BPrivate::AppServerLink winLink;
                                    winLink.StartMessage(AS_BRING_TEAM_TO_FRONT);
                                    winLink.Attach<int32>(systemTokens[i]);
                                    winLink.Flush();
                                }
                                free(cInfo);
                            }
                        }
                        free(systemTokens);
                    }
                    // =========================================================================
                    
                    // Optional: Reset timer after execution so it doesn't endlessly refire link updates
                    // if you keep your mouse parked on the text bar.
                    hoverStartTime = currentTime; 
                }
            } else {
                // The cursor slipped out of the box boundaries; clear the stopwatch register instantly!
                hoverStartTime = 0;
            }
            
            DrawNativeSystemText(displayTitle.String(), listBaseX, listBaseY);
        } else if (!fShouldDrawList) {
            fHoveredTeam = -1;
            fCurrentWindowsList.clear();
        }

        glDisable(GL_BLEND);   
        glPopMatrix();

    } // Exact functional closing brace of RenderFrame() method!









private:

       void UpdateGlobalCpuLoadTracker() {
        uint32 currentTicks = SDL_GetTicks();
        // Sample every 100ms for fast, hyper-responsive bouncing action
        if (currentTicks - fLastCpuPulseTime < 100) return;
        fLastCpuPulseTime = currentTicks;

        system_info info;
        if (get_system_info(&info) != B_OK) return;

        cpu_info* cpuInfos = new cpu_info[info.cpu_count];
        if (get_cpu_info(0, info.cpu_count, cpuInfos) != B_OK) {
            delete[] cpuInfos;
            return;
        }

        // Persistent tracking array for previous ticks per core (declare as static or class members)
        static std::vector<bigtime_t> prevActiveTicks(info.cpu_count, 0);
        static bigtime_t prevSystemTime = system_time();

        bigtime_t currentSystemTime = system_time();
        bigtime_t totalDelta = currentSystemTime - prevSystemTime;
        prevSystemTime = currentSystemTime;

        // Calculate and update the fixed slot load for each core independently
        for (uint32 i = 0; i < info.cpu_count; ++i) {
            bigtime_t activeDelta = cpuInfos[i].active_time - prevActiveTicks[i];
            prevActiveTicks[i] = cpuInfos[i].active_time;

            float coreLoad = 0.0f;
            if (totalDelta > 0) {
                coreLoad = static_cast<float>(activeDelta) / static_cast<float>(totalDelta);
            }

            if (coreLoad < 0.0f) coreLoad = 0.0f;
            if (coreLoad > 1.0f) coreLoad = 1.0f;

            // Store each core directly into its own fixed array slot (fCpuHistory handles up to 40)
            if (i < 40) {
                fCpuHistory[i] = coreLoad;
            }
        }
        delete[] cpuInfos;
        
        // Cache the total count of active cores currently being tracked
        fCpuHistoryIndex = info.cpu_count; 
    }





    HaikuTexture LoadWallpaperViaTranslationKit(const char* filepath) {
        HaikuTexture tex;
        
        // Use Haiku's native translation kit utility to parse ANY common format (PNG, JPEG, etc.)
        BBitmap* haikuBitmap = BTranslationUtils::GetBitmap(filepath);
        if (haikuBitmap == nullptr) {
            std::cerr << "[WALLPAPER WARN] Could not decode image via Translation Kit: " << filepath << std::endl;
            return tex;
        }

        tex.width = (int)haikuBitmap->Bounds().Width() + 1;
        tex.height = (int)haikuBitmap->Bounds().Height() + 1;

        // Generate the texture allocation slots on the GPU
        glGenTextures(1, &tex.id);
        glBindTexture(GL_TEXTURE_2D, tex.id);

        // Linear filtering ensures the image smooths out cleanly if its resolution doesn't match your monitor!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Upload using native BGRA structure matching Little-Endian memory architectures
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGB8, tex.width, tex.height, 0, 
            GL_BGRA, GL_UNSIGNED_BYTE, haikuBitmap->Bits()
        );

        delete haikuBitmap; // Clean up temporary host CPU memory container safely
        return tex;
    }


    void UpdateLiveClockTexture() {
        time_t rawTime = ::time(nullptr);
        struct tm* timeInfo = ::localtime(&rawTime);
        if (!timeInfo) return;

        char timeBuffer[32]; 
        ::strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", timeInfo);
        std::string currentTimeStr(timeBuffer);

        if (!currentTimeStr.empty() && currentTimeStr[0] == '0') {
            currentTimeStr.erase(0, 1);
        }

        if (currentTimeStr != fLastClockTimeString) {
            if (fClockTexture.id != 0) {
                glDeleteTextures(1, &fClockTexture.id);
                fClockTexture.id = 0;
            }

            fLastClockTimeString = currentTimeStr;
            
            // FIX: Pass 32.0f to rasterize a high-res text canvas sheet once a minute
            fClockTexture = RenderTextToTexture(fLastClockTimeString.c_str(), &fClockWidth, &fClockHeight, 32.0f);
        }
    }


	void FetchHaikuMixerVolume() {
	    uint32 ticksNow = SDL_GetTicks();
	    if (ticksNow - fLastVolumeCheckTime < 250) return; // Rate-limit checking to save CPU
	    fLastVolumeCheckTime = ticksNow;
	
	    BMediaRoster* roster = BMediaRoster::Roster();
	    if (!roster) return;
	
	    media_node mixerNode;
	    if (roster->GetAudioMixer(&mixerNode) == B_OK) {
	        BParameterWeb* parameterWeb = nullptr;
	        // Query the active hardware routing configuration graph properties
	        if (roster->GetParameterWebFor(mixerNode, &parameterWeb) == B_OK && parameterWeb != nullptr) {
	            int32 count = parameterWeb->CountParameters();
	            for (int32 i = 0; i < count; i++) {
	                BParameter* param = parameterWeb->ParameterAt(i);
	                // Look for the absolute master output volume gain slider controller item
	                if (param && (param->Type() == BParameter::B_CONTINUOUS_PARAMETER) &&
	                    (strcmp(param->Kind(), B_MASTER_GAIN) == 0 || strcmp(param->Name(), "Master") == 0)) {
	                    
	                    BContinuousParameter* gainSlider = static_cast<BContinuousParameter*>(param);
	                    float rawGain = 0.0f;
	                    bigtime_t lastChanged;
	                    size_t bytesRead = sizeof(float);
	                    
	                    if (gainSlider->GetValue(&rawGain, &bytesRead, &lastChanged) == B_OK) {
	                        float minGain = gainSlider->MinValue();
	                        float maxGain = gainSlider->MaxValue();
	                        // Normalize the raw DB float metrics directly into a clean 0.0f - 1.0f range
	                        fCurrentVolumeLevel = (rawGain - minGain) / (maxGain - minGain);
	                        if (fCurrentVolumeLevel < 0.0f) fCurrentVolumeLevel = 0.0f;
	                        if (fCurrentVolumeLevel > 1.0f) fCurrentVolumeLevel = 1.0f;
	                    }
	                    break;
	                }
	            }
	            delete parameterWeb; // Clean up parameter tree to prevent memory leaks
	        }
	        // Release hardware node thread reference counters
	        roster->ReleaseNode(mixerNode);
	    }
	}




    void DrawFilledRoundedRect(const HaikuRect& rect, float radius, float r, float g, float b, float a) {
        glColor4f(r, g, b, a);
        glBegin(GL_POLYGON);
            // Top Right Corner
            DrawCornerArc(rect.right - radius, rect.top + radius, radius, 270.0f, 360.0f);
            // Bottom Right Corner
            DrawCornerArc(rect.right - radius, rect.bottom - radius, radius, 0.0f, 90.0f);
            // Bottom Left Corner
            DrawCornerArc(rect.left + radius, rect.bottom - radius, radius, 90.0f, 180.0f);
            // Top Left Corner
            DrawCornerArc(rect.left + radius, rect.top + radius, radius, 180.0f, 270.0f);
        glEnd();
    }

    void DrawOutlineRoundedRect(const HaikuRect& rect, float radius, float r, float g, float b, float a) {
        glColor4f(r, g, b, a);
        glLineWidth(1.0f);
        glBegin(GL_LINE_LOOP);
            DrawCornerArc(rect.right - radius, rect.top + radius, radius, 270.0f, 360.0f);
            DrawCornerArc(rect.right - radius, rect.bottom - radius, radius, 0.0f, 90.0f);
            DrawCornerArc(rect.left + radius, rect.bottom - radius, radius, 90.0f, 180.0f);
            DrawCornerArc(rect.left + radius, rect.top + radius, radius, 180.0f, 270.0f);
        glEnd();
    }

    void DrawCornerArc(float cx, float cy, float radius, float startAngle, float endAngle) {
        // Step through angles to draw a smooth quarter circle arc frame segment
        const float degToRad = 3.14159265f / 180.0f;
        for (float angle = startAngle; angle <= endAngle; angle += 10.0f) {
            float rad = angle * degToRad;
            glVertex2f(cx + radius * std::cos(rad), cy + radius * std::sin(rad));
        }
    }
    
	HaikuTexture RenderWhiteTextToTexture(const char* labelText, int* outWidth, int* outHeight, float targetFontSize = -1.0f) {
	    HaikuTexture textTex;
	    
	    // 1. Configure the font to use B_BOLD_FACE for high readability
	    BFont localFont(be_plain_font);
	    localFont.SetFace(B_BOLD_FACE);
	    
	    if (targetFontSize > 0.0f) {
	        localFont.SetSize(targetFontSize);
	    } else {
	        localFont.SetSize(12.0f); 
	    }
	    
	    // Add extra horizontal breathing padding specifically for bold text sizing
	    float stringPixelWidth = localFont.StringWidth(labelText);
	    font_height fontMetrics;
	    localFont.GetHeight(&fontMetrics);
	    float fontTotalHeight = fontMetrics.ascent + fontMetrics.descent + fontMetrics.leading;
	
	    // Expand the allocation dimensions to perfectly containerize our rounded capsule edges
	    int bitmapW = (int)(stringPixelWidth + 16.0f); 
	    int bitmapH = (int)(fontTotalHeight + 10.0f);
	    
	    if (bitmapW % 2 != 0) bitmapW++;
	
	    *outWidth = bitmapW;
	    *outHeight = bitmapH;
	
	    BRect drawingBounds(0, 0, bitmapW - 1, bitmapH - 1);
	    BBitmap* textBitmap = new BBitmap(drawingBounds, B_RGBA32, true);
	    
	    memset(textBitmap->Bits(), 0, textBitmap->BitsLength());
	
	    BView* drawTarget = new BView(drawingBounds, "text_raster_view", B_FOLLOW_NONE, B_WILL_DRAW);
	    textBitmap->AddChild(drawTarget);
	
	    if (textBitmap->Lock()) {
	        // Clear background pixels to absolute transparency
	        drawTarget->SetLowColor(B_TRANSPARENT_COLOR); 
	        drawTarget->FillRect(drawTarget->Bounds(), B_SOLID_LOW);
	        
	        // Configure native alpha compositing pass
	        drawTarget->SetDrawingMode(B_OP_ALPHA);
	        drawTarget->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
	        
	        // 2. Draw a native, perfect anti-aliased dark background capsule with rounded corners
	        drawTarget->SetHighColor(15, 15, 15, 190); // 190 alpha sleek translucent dark bubble
	        drawTarget->FillRoundRect(drawTarget->Bounds(), 5.0f, 5.0f); // 5.0f smooth corner radii
	
	        // 3. Render the solid white bold system text safely on top of the capsule
	        drawTarget->SetHighColor(255, 255, 255, 255); 
	        drawTarget->SetFont(&localFont);
	        
	        // Center the bold layout string context vertically and horizontally inside our bounding container
	        float textX = 8.0f;
	        float textY = fontMetrics.ascent + 5.0f;
	        drawTarget->DrawString(labelText, BPoint(textX, textY));
	        
	        drawTarget->Sync(); 
	        textBitmap->Unlock();
	    }
	
	    glGenTextures(1, &textTex.id);
	    glBindTexture(GL_TEXTURE_2D, textTex.id);
	    
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	    glTexImage2D(
	        GL_TEXTURE_2D, 0, GL_RGBA8, bitmapW, bitmapH, 0, 
	        GL_BGRA, GL_UNSIGNED_BYTE, textBitmap->Bits()
	    );
	
	    delete textBitmap; 
	    return textTex;
	}

   
    

    HaikuTexture RenderTextToTexture(const char* labelText, int* outWidth, int* outHeight, float targetFontSize = -1.0f) {
        HaikuTexture textTex;
        
        // 1. Setup a dynamic local font object to override point sizes cleanly
        BFont localFont(be_plain_font);
        if (targetFontSize > 0.0f) {
            localFont.SetSize(targetFontSize);
        } else {
            // Default baseline fallback if no font size is explicitly provided
            localFont.SetSize(12.0f); 
        }
        
        // Fetch font preferences and text metrics using our dynamic font instance
        float stringPixelWidth = localFont.StringWidth(labelText);
        font_height fontMetrics;
        localFont.GetHeight(&fontMetrics);
        float fontTotalHeight = fontMetrics.ascent + fontMetrics.descent + fontMetrics.leading;

        int bitmapW = (int)(stringPixelWidth + 6.0f);
        int bitmapH = (int)(fontTotalHeight + 4.0f);
        
        if (bitmapW % 2 != 0) bitmapW++;

        *outWidth = bitmapW;
        *outHeight = bitmapH;

        // 2. Allocate an offscreen bitmap surface layer with an alpha channel
        BRect drawingBounds(0, 0, bitmapW - 1, bitmapH - 1);
        BBitmap* textBitmap = new BBitmap(drawingBounds, B_RGBA32, true);
        
        memset(textBitmap->Bits(), 0, textBitmap->BitsLength());

        BView* drawTarget = new BView(drawingBounds, "text_raster_view", B_FOLLOW_NONE, B_WILL_DRAW);
        textBitmap->AddChild(drawTarget);

        if (textBitmap->Lock()) {
            // FIX: Rasterize text as crisp solid BLACK to match dock tray styling.
            // This prevents sub-pixel anti-aliasing color bleeding in OpenGL.
            drawTarget->SetHighColor(0, 0, 0, 255); 
            drawTarget->SetLowColor(0, 0, 0, 0); 
            
            drawTarget->SetDrawingMode(B_OP_ALPHA);
            drawTarget->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
            
            drawTarget->SetFont(&localFont); // Use our custom-sized font instance
            
            drawTarget->DrawString(labelText, BPoint(3.0f, fontMetrics.ascent + 2.0f));
            drawTarget->Sync(); 
            textBitmap->Unlock();
        }

        // 3. Register and upload the text image matrix data blocks directly to OpenGL
        glGenTextures(1, &textTex.id);
        glBindTexture(GL_TEXTURE_2D, textTex.id);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, bitmapW, bitmapH, 0, 
            GL_BGRA, GL_UNSIGNED_BYTE, textBitmap->Bits()
        );

        delete textBitmap; 
        return textTex;
    }




	void DrawFilledRect(const HaikuRect& rect, float r, float g, float b, float a = 1.0f) {
	    glColor4f(r, g, b, a); // 4f adds the Alpha opacity channel!
	    glBegin(GL_QUADS);
	        glVertex2f(rect.left,  rect.top);
	        glVertex2f(rect.right, rect.top);
	        glVertex2f(rect.right, rect.bottom);
	        glVertex2f(rect.left,  rect.bottom);
	    glEnd();
	}


    // Helper function that handles real files, directories, and symlink resolution paths
    HaikuTexture LoadIconFromNode(const char* filepath, int targetSize) {
        HaikuTexture texture;
        
        // 1. Instantiate a file entry handle to check for symlinks
        BEntry entry(filepath, true); // Setting the second argument to true forces auto-traversal!
        
        // Safety check: If a link is completely broken, fall back to a raw un-traversed path
        if (entry.InitCheck() != B_OK || !entry.Exists()) {
            entry.SetTo(filepath, false); 
        }

        // Fetch the absolute resolved path of the target item
        BPath resolvedPath;
        entry.GetPath(&resolvedPath);

        // 2. Open the true resolved node target
        BNode node(resolvedPath.Path());
        BNodeInfo nodeInfo(&node);
        
        if (node.InitCheck() != B_OK || nodeInfo.InitCheck() != B_OK) return texture;

        BRect bounds(0, 0, targetSize - 1, targetSize - 1);
        BBitmap* haikuBitmap = new BBitmap(bounds, B_RGBA32);

        // Pull the icon information from the true target node
        if (nodeInfo.GetTrackerIcon(haikuBitmap, icon_size(targetSize)) == B_OK) {
            texture.width = targetSize;
            texture.height = targetSize;

            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            // Support Haiku Standard Mesa as well as x512's nebula driver with this update.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, targetSize, targetSize, 0, GL_BGRA, GL_UNSIGNED_BYTE, haikuBitmap->Bits());
        }
        
        delete haikuBitmap;
        return texture;
    }


    int fWidth, fHeight;
    float fBgColorR, fBgColorG, fBgColorB;
    HaikuRect fDeskbarBounds;

    // Vector list array tracking real system files dynamically
    std::vector<DesktopIconItem> fDesktopItems;
    
    int fMouseX;
    int fMouseY;
    
    HaikuTexture fClockTexture;
    int          fClockWidth = 0;
    int          fClockHeight = 0;
    std::string  fLastClockTimeString = "";
    
    HaikuTexture fWallpaperTexture;
    HaikuTexture fHaikuMenuIcon;
    HaikuTexture fHaikuTrashIcon;
    
    bool      fShowMainMenu = false;
    HaikuRect fMainMenuBounds = { 0.0f, 0.0f, 0.0f, 0.0f };    

    // Tracking parameters for mouse interaction
    uint32 fLastClickTime = 0;
    int fLastClickedIndex = -1;  
    
    float fScrollOffset = 0.0f;
    float fMaxScrollOffset = 0.0f;

    // CPU Global Graph Pulse Metrics (Ring Buffer tracking the last 40 seconds)
    float fCpuHistory[40] = { 0.0f };
    int   fCpuHistoryIndex = 0;
    uint32 fLastCpuPulseTime = 0;

    // Tracking the previous raw CPU ticks to compute accurate differential delta load
    bigtime_t fPrevActiveTicks = 0;
    bigtime_t fPrevTotalTicks = 0;    
    
    HaikuTexture fCpuTooltipTex;
    int          fCpuTooltipW = 0, fCpuTooltipH = 0;
    std::string  fLastCpuTooltipStr = "";
    
	bool  fIsResizing = false;
    float fResizeStartX = 0.0f;
    float fResizeStartY = 0.0f;
    float fResizeStartWidth = 0.0f;
    float fResizeStartHeight = 0.0f;
    
    std::vector<TaskbarItem> fTaskbarWindows;
    HaikuRect fTrashRect; 
    
    uint32 fLastTrashCheckTime;   
    float fTrashTooltipAlpha = 0.0f; 
    
    unsigned int fTrashTooltipTexId = 0; 
    int          fTrashTooltipW = 0;
    int          fTrashTooltipH = 0;
    bool         fTrashTextGenerated = false;
    
    float fCurrentVolumeLevel = 0.5f; // Active volume state mapping cache (0.0f to 1.0f)
	uint32 fLastVolumeCheckTime = 0;
	bool fIsDraggingVolumeSlider = false; // Persistent drag flag
	float fCachedVolLeft = 0.0f;          // Saved screen positions
	float fCachedVolTop = 0.0f;
	float fCachedVolWidth = 0.0f;
	float fCachedVolHeight = 0.0f;
	float fPreMuteVolumeLevel = 0.5f; 
	uint32 fLastTrackerMenuCloseTime; 
	bool fTrackerMenuIsActive = false;
	
//@private    

public:
    float fLastCalculatedWidth = 0.0f;	
    bool fCpuMenuIsActive; 
    bool fCursorIsInsideHitbox = false;
    bool fLeafMenuIsActive = false;
	uint32 fLastLeafMenuCloseTime = 0;
};


class WallpaperWatcher : public BHandler {
private:
    node_ref fDesktopNodeRef;

public:
    WallpaperWatcher() : BHandler("WallpaperWatcher") {
        BPath desktopPath;
        if (find_directory(B_DESKTOP_DIRECTORY, &desktopPath) == B_OK) {
            BNode desktopNode(desktopPath.Path());
            if (desktopNode.InitCheck() == B_OK) {
                // Get the unique node reference identifiers
                desktopNode.GetNodeRef(&fDesktopNodeRef);
                
                // Start watching for any attribute changes on the desktop directory node
                if (be_app && be_app->Lock()) {
                    be_app->AddHandler(this);
                    watch_node(&fDesktopNodeRef, B_WATCH_ATTR, this);
                    be_app->Unlock();
                }
            }
        }
    }

    virtual ~WallpaperWatcher() {
        // Safely unregister monitoring on deletion
        stop_watching(this);
        if (be_app && be_app->Lock()) {
            be_app->RemoveHandler(this);
            be_app->Unlock();
        }
    }

    virtual void MessageReceived(BMessage* message) {
        if (message->what == B_NODE_MONITOR) {
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) == B_OK && opcode == B_ATTR_CHANGED) {
                const char* attrName;
                if (message->FindString("attr", &attrName) == B_OK && strcmp(attrName, "be:bgndimginfo") == 0) {
                    // Trigger a thread-safe custom event directly into the SDL processing stream
                    SDL_Event userEvent;
                    SDL_zero(userEvent);
                    userEvent.type = SDL_EVENT_WALLPAPER_CHANGED;
                    SDL_PushEvent(&userEvent);
                }
            }
        } else {
            BHandler::MessageReceived(message);
        }
    }
};



void SaveConfiguration() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("hdesktop_settings");
        
        BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        BMessage settings;
        
        // 1. Pack existing settings
        settings.AddBool("auto_hide",     autoHideEnabled);
        settings.AddBool("sys_tray",      showSystemTray);
        settings.AddBool("auto_raise",    dockAlwaysOnTop);
        settings.AddBool("text_overlays", fShowTitleOverlays);
        settings.AddFloat(kSettingsIconSizeKey, fBaseIconSize);
        settings.AddFloat(kSettingsAlphaKey, fDockAlpha);
        // 2. Pack all live favorites keys sequentially into the same field name
        std::set<std::string>::iterator it;
        for (it = gFavoritePaths.begin(); it != gFavoritePaths.end(); ++it) {
            settings.AddString("favorite_apps", it->c_str());
        }
        
        settings.Flatten(&file); 
    }
}



void LoadConfiguration() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) return;
    path.Append("hdesktop_settings");

    BFile file(path.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) {
        fDockAlpha = 0.50f;
        fBaseIconSize = 48.0f; // Safe fallback initial fallback default
        return;
    }

    BMessage settings;
    if (settings.Unflatten(&file) == B_OK) {
        settings.FindBool("auto_hide", &autoHideEnabled);
        settings.FindBool("sys_tray", &showSystemTray);
        settings.FindBool("auto_raise", &dockAlwaysOnTop);
        settings.FindBool("text_overlays", &fShowTitleOverlays);
        settings.FindFloat(kSettingsAlphaKey, &fDockAlpha);
        
        // NEW: Unpack icon size safely. Fall back to 48.0f if missing
        if (settings.FindFloat(kSettingsIconSizeKey, &fBaseIconSize) != B_OK) {
            fBaseIconSize = 48.0f;           
         
        }
        
         // Recover the favorites string index array
         const char* favPath = nullptr;
         int32 i = 0;
         // Loops through every entry inside the key array automatically
         while (settings.FindString("favorite_apps", i, &favPath) == B_OK) {
             if (favPath != nullptr) {
                 gFavoritePaths.insert(favPath);
             }
           i++;
        }     
    }
}




// =========================================================================
// ASYNC CPU MENU RUNNER 
// =========================================================================
void AsyncCpuMenuRunner::_DisplayCPUGraphMenu() {
    // 1. Create the base context menu shell container.
    BPopUpMenu* pcMenu = new BPopUpMenu("CPUGraphContext", false, false);
    pcMenu->SetRadioMode(false);

    // =========================================================================
    // DYNAMIC SUBMENU 1: QUIT AN APPLICATION (RESTORED WITH ROBUST ICON TRACKING)
    // =========================================================================
    BMenu* quitAppMenu = new BMenu("Quit an application");
    
    team_info teamInfo;
    int32 cookie = 0; 
    bool addedApps = false;

    while (get_next_team_info(&cookie, &teamInfo) == B_OK) {
        std::string fullArgs(teamInfo.args);
        
        size_t lastSlash = fullArgs.find_last_of('/');
        std::string appName = (lastSlash != std::string::npos) ? fullArgs.substr(lastSlash + 1) : fullArgs;
        
        size_t firstSpace = appName.find_first_of(" \t\r\n");
        if (firstSpace != std::string::npos) {
            appName = appName.substr(0, firstSpace);
        }

        if (appName.empty() || teamInfo.team == 1 || appName == "kernel") {
            continue;
        }

        // Keep core servers isolated from unexpected/accidental close clicks
        if (appName == "app_server" || appName == "input_server" || 
            appName == "registrar"  ||
            appName == "syslog_daemon") {
            continue;
        }

        // Initialize the messaging payload parameters
        BMessage* killMsg = new BMessage('kill');
        killMsg->AddInt32("target_team", teamInfo.team);
        killMsg->AddString("target_name", appName.c_str());
        
        BBitmap* miniIcon = nullptr;

        // Extract system vector/bitmap graphics via image structures
        image_info imgInfo;
        int32 imgCookie = 0;
        
        if (get_next_image_info(teamInfo.team, &imgCookie, &imgInfo) == B_OK) {
            BEntry appEntry(imgInfo.name);
            if (appEntry.Exists()) {
                entry_ref ref;
                if (appEntry.GetRef(&ref) == B_OK) {
                    BRect iconBounds(0, 0, 15, 15);
                    BBitmap* tempIcon = new BBitmap(iconBounds, B_RGBA32);
                    
                    if (BNodeInfo::GetTrackerIcon(&ref, tempIcon, B_MINI_ICON) == B_OK) {
                        miniIcon = tempIcon; 
                    } else {
                        delete tempIcon; 
                    }
                }
            }
        }

        // Use your custom subclass to bind the application icon alongside its label
        BIconMenuItem* processItem = new BIconMenuItem(appName.c_str(), killMsg, miniIcon);
        quitAppMenu->AddItem(processItem);
        addedApps = true;
    }

    if (!addedApps) {
        BMenuItem* emptyItem = new BMenuItem("No system applications running", nullptr);
        emptyItem->SetEnabled(false);
        quitAppMenu->AddItem(emptyItem);
    }
    
    pcMenu->AddItem(quitAppMenu);

    // =========================================================================
    // DYNAMIC SUBMENU 2 & 3: MEMORY AND LIVE-THREAD RECYCLERS
    // =========================================================================
    BLiveMemoryMenu* memUsageMenu = new BLiveMemoryMenu("Memory usage");
    pcMenu->AddItem(memUsageMenu);
    
    BRealtimeCpuMenu* threadCpuMenu = new BRealtimeCpuMenu("Threads and CPU usage");
    pcMenu->AddItem(threadCpuMenu);

    // =========================================================================
    // CORE NATIVE UTILITIES & MISC ACTIONS
    // =========================================================================
    pcMenu->AddItem(new BMenuItem("Power saving", new BMessage('pwrS')));

    // =========================================================================
    // POSITION CALCULATIONS AND SYNCHRONOUS RUNNER INVOKATION
    // =========================================================================
    float anchoredMenuX = static_cast<float>(fArgs->winX + fArgs->mouseX) - 45.0f;
    if (anchoredMenuX < 0.0f) anchoredMenuX = 5.0f;
    
    // SMART ADJUSTMENT: Calculate layout normalization metrics boundary limit tracker
    float maxExpectedHeight = 164.0f; 
    float structuralOffset = maxExpectedHeight - fArgs->currentDockH;
    if (structuralOffset < 0.0f) structuralOffset = 0.0f; // Clamp shield protection
    
    // FIX: Push it lower down the screen boundary context as your dock container shrivels
    float anchoredMenuY = static_cast<float>(fArgs->winY) + structuralOffset - 5.0f;
    BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);

    // Notice we use Go(..., false, false) intentionally here!
    // Since this method runs exclusively within our separate worker looper window thread, 
    // blocking synchronously here is completely safe and won't lock your main SDL loop.
    BMenuItem* chosenAction = pcMenu->Go(screenClickPoint, false, false);

    // FIX: Release the safety shield flag instantly when the menu collapses or a selection finishes
    if (fArgs && fArgs->engine) {
        fArgs->engine->fCpuMenuIsActive = false;
    }

    // =========================================================================
    // ROUTING AND SIGNAL HANDLING MATRIX (WITH MODAL CONFIRMATION ALERTS)
    // =========================================================================

    if (chosenAction != nullptr) {
        BMessage* actionMsg = chosenAction->Message();
        if (actionMsg != nullptr) {
            switch (actionMsg->what) {
                case 'kthr': {
                    team_id targetTeam = -1;
                    const char* thName = "Unknown";
                    
                    if (actionMsg->FindInt32("target_thread", &targetTeam) == B_OK) {
                        actionMsg->FindString("target_name", &thName);
                        
                        char alertText[256];
                        std::snprintf(alertText, sizeof(alertText), 
                            "Are you sure you want to force terminate the process '%s' (Team ID: %d)?\n\n"
                            "Unsaved progress inside this application will be lost.", thName, targetTeam);

                        BAlert* confirmationBox = new BAlert("Force Terminate", alertText, 
                            "Cancel", "Force Kill", nullptr, 
                            B_WIDTH_AS_USUAL, B_WARNING_ALERT);
                        
                        confirmationBox->SetShortcut(0, B_ESCAPE);

                        // =========================================================================
                        // CENTER ON SCREEN FIX
                        // =========================================================================
                        confirmationBox->CenterOnScreen(); // Automatically calculates center bounds

                        int32 userChoice = confirmationBox->Go();

                        if (userChoice == 1) { 
                            kill_team(targetTeam); 
                        } 
                    }
                    break;
                }

                case 'kill': {
                    team_id targetTeam = -1;
                    const char* appName = "Unknown";
                    
                    if (actionMsg->FindInt32("target_team", &targetTeam) == B_OK) {
                        actionMsg->FindString("target_name", &appName);
                        
                        char alertText[256];
                        std::snprintf(alertText, sizeof(alertText), 
                            "Do you want to close '%s' smoothly?\n\n"
                            "This will send a standard quit request to the application loop layer.", appName);

                        BAlert* confirmationBox = new BAlert("Close Application", alertText, 
                            "Cancel", "Close App", nullptr, 
                            B_WIDTH_AS_USUAL, B_INFO_ALERT);
                        
                        confirmationBox->SetShortcut(0, B_ESCAPE);

                        // =========================================================================
                        // CENTER ON SCREEN FIX
                        // =========================================================================
                        confirmationBox->CenterOnScreen(); // Automatically calculates center bounds

                        int32 userChoice = confirmationBox->Go();

                        if (userChoice == 1) {                             
                            BMessenger appTarget(nullptr, targetTeam);
                            if (appTarget.IsValid()) {
                                appTarget.SendMessage(B_QUIT_REQUESTED);
                            } else {
                                kill_team(targetTeam); 
                            }
                        } 
                    }
                    break;
                }


                case 'pwrS': {
                        std::system("/boot/system/apps/PowerStatus --toggle &"); 
                    break;
                }

                default:
                    break;
            }
        }
    }


    // Release the active menu latch safety shield on your engine instance before exiting
    fArgs->engine->fCpuMenuIsActive = false;

    // Clean up our instances inside our thread bubble
    delete pcMenu;
    delete fArgs;
}




// =========================================================================
// AUTOHIDE CONFIGURATION & STATES (STAGE 1)
// =========================================================================
enum AutoHideState {
    STATE_VISIBLE,
    STATE_HIDING,
    STATE_HIDDEN,
    STATE_SHOWING
};



// =========================================================================
// MAIN SDL2 SYSTEM WRAPPER CONTAINER PIPELINE ENTRYPOINT
// =========================================================================
int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL Subsystem initialization failure: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // =========================================================================
    // RESOLVE MONITOR RESOLUTION BOUNDS DYNAMICALLY
    // =========================================================================
    SDL_DisplayMode currentDisplayMode;
    if (SDL_GetCurrentDisplayMode(0, &currentDisplayMode) != 0) {
        std::cerr << "Display tracking lookup failure: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // =========================================================================
    // RESOLVE MONITOR RESOLUTION BOUNDS DYNAMICALLY
    // =========================================================================
    int screenWidth  = currentDisplayMode.w;
    int screenHeight = currentDisplayMode.h;
    
    // Choose a safe, sensible base height for the window to open with initially
    int dockPanelW = screenWidth;
    int dockPanelH = 140; 
    int sensorHeight = 4; 

    // Use yExpanded to position the actual SDL window frame at the bottom
    int yExpanded  = screenHeight - dockPanelH; 

    // Animation tracking (0.0f means elements draw normally inside the bottom window)
    float currentY = 0.0f;
    float targetY  = 0.0f;
    AutoHideState dockState = STATE_VISIBLE;
    bool hidingSettled = false;
	LoadConfiguration(); 
	WallpaperWatcher wallpaperWatcher;
	
    // FIX: Force the initial window creation down to the bottom using yExpanded!
    SDL_Window* window = SDL_CreateWindow(
        "Haiku Desktop Taskbar Overlay Component",
        0, yExpanded,
        dockPanelW, dockPanelH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS
        //SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP
    );


    if (!window) {
        std::cerr << "OpenGL Window creation aborted: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }


    SDL_GLContext glContext = SDL_GL_CreateContext(window);

    if (!glContext) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    

    SDL_GL_SetSwapInterval(1);
	glViewport(0, 0, screenWidth, dockPanelH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // --- WALLPAPER RE-STITCH ALIGNMENT MATH ---
    float panelTopY = static_cast<float>(screenHeight) - static_cast<float>(dockPanelH);
   //float panelTopY    = static_cast<float>(screenHeight) - 140.0f;
    float panelBottomY = static_cast<float>(screenHeight);
    
    gluOrtho2D(0.0, static_cast<float>(screenWidth), panelBottomY, panelTopY);    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();    
    HaikuGlDesktopEngine desktopEngine(screenWidth, screenHeight);


    bool appExecuting = true;
    SDL_Event incomingEventPackage;
    
    
    // Update Chcker
   	{
    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/hdesktop/refs/heads/main/VERSION";
    const char* localVersion = "v1.0.34"; 
    char updateCmd[1024];
    snprintf(updateCmd, sizeof(updateCmd),
    	#ifndef IS_HAIKU_32BIT
        "(REMOTE_V=$(curl -sL \"%s\" | tr -d '\\r\\n'); "
        #else
        "(REMOTE_V=$(curl-x86 -sL \"%s\" | tr -d '\\r\\n'); "
        #endif
        "if [ ! -z \"$REMOTE_V\" ] && [ \"$REMOTE_V\" != \"%s\" ]; then "
        "notify --title \"Update Available\" --group \"hDesktop\" "
        "\"A newer version of hDesktop is available! ($REMOTE_V)\"; fi) &",
        targetUrl, localVersion);	
    system(updateCmd);
   }
    



    // =========================================================================
    // RENDER-ON-DEMAND EVENT PIPELINE (DROPS CPU TO ~0%)
    // =========================================================================
    uint32 lastRosterScanTime = 0;
    uint32 lastMetricsUpdateTime = 0;
    bool needsRender = true;
    
        // --- TIMING ENGINES FOR SMOOTH ANIMATION ---
    Uint64 lastPerfTime = SDL_GetPerformanceCounter();
    Uint64 perfFrequency = SDL_GetPerformanceFrequency();

    // Adjusted base speed parameter (now scaled against real seconds)
    float baseAnimationSpeed = 12.0f; 
    int localMouseX = 0;
    int localMouseY = 0;
    uint32 nativeButtons = 0;
    bool cursorIsInsideDock = false;

    while (appExecuting) {
        if (SDL_WaitEventTimeout(&incomingEventPackage, 30)) {
            do {
                // =========================================================================
                // ANTI-FOCUS HIJACK INTERCEPTION PROTOCOL
                // =========================================================================
                
                if (incomingEventPackage.type == SDL_WINDOWEVENT) {
                    if (incomingEventPackage.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                        incomingEventPackage.window.event == SDL_WINDOWEVENT_TAKE_FOCUS) {
                        
                        if (be_app && be_app->Lock()) {
                            int32 windowCount = be_app->CountWindows();
                            for (int32 i = 0; i < windowCount; i++) {
                                BWindow* win = be_app->WindowAt(i);
                                if (win != nullptr && win->Lock()) {
                                    win->SendBehind(nullptr); // Sink to the bottom
                                    win->Unlock();
                                    break;
                                }
                            }
                            be_app->Unlock();
                        }
                        continue; 
                    }
                }
                
                // =========================================================================

                if (incomingEventPackage.type == SDL_QUIT) {
                    appExecuting = false;
                }
                // =========================================================================
                // NATIVE WALLPAPER MONITOR INTERACTION PROTOCOL
                // =========================================================================
                else if (incomingEventPackage.type == SDL_EVENT_WALLPAPER_CHANGED) {                   
                    desktopEngine.ReloadWallpaperBackground(); 
                    desktopEngine.SyncDockWithRunningDeskbarApps(); 

                    
                    needsRender = true;
                }   
                // =========================================================================
                else if (incomingEventPackage.type == SDL_KEYDOWN) {

                    if (incomingEventPackage.key.keysym.sym == SDLK_ESCAPE) {
                        appExecuting = false;
                    }
                }
                else if (incomingEventPackage.type == SDL_MOUSEMOTION || 
                         incomingEventPackage.type == SDL_MOUSEBUTTONDOWN ||
                         incomingEventPackage.type == SDL_MOUSEBUTTONUP) {
                    
                    int mouseX, mouseY;
                    Uint32 buttons = SDL_GetMouseState(&mouseX, &mouseY);
                
                    if (dockState == STATE_HIDDEN && !cursorIsInsideDock) {
                        needsRender = true;
                        continue;
                    }
                
                    int hiddenScreenOffset = screenHeight - dockPanelH;
                    int adjustedMouseY = mouseY + hiddenScreenOffset;
                
                    // Feed smooth radial zoom parameters
                    desktopEngine.HandleMouseInput(mouseX, adjustedMouseY, buttons);
                
                    if (incomingEventPackage.type == SDL_MOUSEBUTTONDOWN) {
                        if (incomingEventPackage.button.button == SDL_BUTTON_LEFT || 
                            incomingEventPackage.button.button == SDL_BUTTON_RIGHT ||
                            incomingEventPackage.button.button == SDL_BUTTON_MIDDLE) {

                            desktopEngine.HandleMouseClick(mouseX, adjustedMouseY, incomingEventPackage.button.button);


				        }
				        
				    }
				
				    needsRender = true; 
				}


                else if (incomingEventPackage.type == SDL_MOUSEWHEEL) {
                    desktopEngine.HandleMouseWheel(incomingEventPackage.wheel.y);
                    needsRender = true; 
                }        
            } while (SDL_PollEvent(&incomingEventPackage)); 
        }

        // =========================================================================
        // NATIVE HAIKU BOUNDARY & INTERNAL SLIDE TRIGGER LOGIC (STAGE 3 - FIXED)
        // =========================================================================
        if (be_app && be_app->Lock()) {
            int32 windowCount = be_app->CountWindows();
            if (windowCount > 0) {
                BWindow* nativeWin = be_app->WindowAt(0);
                if (nativeWin && nativeWin->Lock()) {
                    BView* mainView = nativeWin->ChildAt(0); 
                    if (mainView) {
                        BPoint localPoint;
                        mainView->GetMouse(&localPoint, &nativeButtons, false);
                        
                        localMouseX = static_cast<int>(localPoint.x);
                        localMouseY = static_cast<int>(localPoint.y);
                        
                        // If hidden, check if cursor hit the tiny sensor row at the bottom
                        // If visible or animating, check the entire active height of the window
                        if (dockState == STATE_HIDDEN) {
                            cursorIsInsideDock = (localMouseX >= 0 && localMouseX < dockPanelW &&
                                                  localMouseY >= (dockPanelH - sensorHeight) && localMouseY < dockPanelH);
                        } else {
                            cursorIsInsideDock = (localMouseX >= 0 && localMouseX < dockPanelW &&
                                                  localMouseY >= 0 && localMouseY < dockPanelH);
                        }
                    }
                    nativeWin->Unlock();
                }
            }
            be_app->Unlock();
        }

        // =========================================================================
        // NATIVE HAIKU BOUNDARY & LOCAL OFFSET CONSTRAINTS (STAGE 11 - PADDED HITBOX)
        // =========================================================================
        // 1. Fetch your active animated width
        float currentDynamicWidth = desktopEngine.fLastCalculatedWidth;
        if (currentDynamicWidth <= 0.0f) currentDynamicWidth = 600.0f; 

        // 2. DEFINE PHANTOM PADDING BUFFERS
        // FIX: Re-expand horizontal padding to give the zoom engine plenty of runway
        int horizontalPaddingZoom = 250; // Wide buffer to catch mouse early for gradual scaling
        int horizontalPaddingPlate = 30;  // Tight buffer matched to the physical dock edges
        int verticalTopPadding = 12;      // Clearance padding for text overlays and menus

        // Calculate the wide limits for the zoom engine
        int paddedLeftX_Zoom  = (dockPanelW / 2) - (static_cast<int>(currentDynamicWidth) / 2) - horizontalPaddingZoom;
        int paddedRightX_Zoom = (dockPanelW / 2) + (static_cast<int>(currentDynamicWidth) / 2) + horizontalPaddingZoom;

        // Calculate the tight limits for window layering adjustments
        int paddedLeftX_Plate  = (dockPanelW / 2) - (static_cast<int>(currentDynamicWidth) / 2) - horizontalPaddingPlate;
        int paddedRightX_Plate = (dockPanelW / 2) + (static_cast<int>(currentDynamicWidth) / 2) + horizontalPaddingPlate;

        // 3. TRACKER 1: Primary Dock Hitbox (Wide for Smooth Zooming Mechanics)
        if (dockState == STATE_HIDDEN) {
            cursorIsInsideDock = (localMouseX >= paddedLeftX_Zoom && localMouseX <= paddedRightX_Zoom &&
                                  localMouseY >= (dockPanelH - sensorHeight) && localMouseY < dockPanelH);
        } else {
            cursorIsInsideDock = (localMouseX >= paddedLeftX_Zoom && localMouseX <= paddedRightX_Zoom &&
                                  localMouseY >= (static_cast<int>(currentY) - verticalTopPadding) && localMouseY < dockPanelH);
        }

        // TRACKER 2: Physical Plate Check (Tight to prevent wide window-focus stealing)
        bool cursorIsOverPhysicalPlate = false;
        
        // FIX: Only evaluate the physical plate if the dock is fully deployed or actively sliding out.
        // We include currentY in the calculation so the focus boundary follows the physical graphic asset!
        if (dockState == STATE_VISIBLE || dockState == STATE_SHOWING) {
            int visualDockHeight = static_cast<int>(fBaseIconSize + 80.0f); // Approximate height of the physical plate asset
            int livePlateTopBound = (dockPanelH - visualDockHeight) + static_cast<int>(currentY);
            
            cursorIsOverPhysicalPlate = (localMouseX >= paddedLeftX_Plate && localMouseX <= paddedRightX_Plate &&
                                         localMouseY >= (livePlateTopBound - verticalTopPadding) && localMouseY < dockPanelH);
        }


        // Pass the wide tracking flag to the background rendering engine
        desktopEngine.fCursorIsInsideHitbox = cursorIsInsideDock;

          // =========================================================================
        // EDGE-TRIGGERED NATIVE HOVER LAYERING SYSTEM
        // =========================================================================
        static bool lastHoverState = false; 
        
        // FIX: Drive the focus rules smoothly by tracking the physical plate status alongside visibility state
        if (dockAlwaysOnTop && ((cursorIsOverPhysicalPlate != lastHoverState) || (dockState == STATE_VISIBLE && lastHoverState))) {
            lastHoverState = cursorIsOverPhysicalPlate; 
            
            bool trackerSubmenuIsOpen = false;
            app_info activeAppInfo;
           
            if (be_roster && be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
                if (strcmp(activeAppInfo.signature, "application/x-vnd.Be-TRAK") == 0) {
                    int32 currentWorkspace = current_workspace();
                    int32* tokens = nullptr;
                    int32 totalTokens = 0;
                    
                    if (BPrivate::get_window_order(currentWorkspace, &tokens, &totalTokens) == B_OK && tokens != nullptr) {
                        for (int32 i = 0; i < totalTokens; i++) {
                            client_window_info* wInfo = get_window_info(tokens[i]);
                            if (wInfo != nullptr) {
                                if (wInfo->team == activeAppInfo.team) {
                                    if (wInfo->feel == 1025) {
                                        trackerSubmenuIsOpen = true;
                                        free(wInfo);
                                        break; 
                                    }
                                }
                                free(wInfo);
                            }
                        }
                        free(tokens);
                    }
                }
            }

            // PERMISSION RULE:
            if (!trackerSubmenuIsOpen) {
                if (be_app && be_app->Lock()) {
                    int32 windowCount = be_app->CountWindows();
                    for (int32 i = 0; i < windowCount; i++) {
                        BWindow* win = be_app->WindowAt(i);
                        if (win != nullptr && win->Lock()) {
                            uint32 flags = win->Flags();

                            // FIX: Only float the window over other apps if the mouse is touching the 
                            // physical plate AND the dock animation has successfully finished deploying (STATE_VISIBLE)
                            if (cursorIsOverPhysicalPlate && dockState == STATE_VISIBLE) {
                                if (win->Feel() != B_FLOATING_ALL_WINDOW_FEEL) {
                                    win->SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
                                    flags &= ~B_AVOID_FRONT;
                                    flags &= ~B_AVOID_FOCUS;
                                    win->Activate(true);
                                }
                            } else {
                                // Return to normal depth layering when hidden or in transparent space
                                if (win->Feel() != B_NORMAL_WINDOW_FEEL) {
                                    win->SetFeel(B_NORMAL_WINDOW_FEEL);
                                    flags |= B_AVOID_FRONT;
                                    flags |= B_AVOID_FOCUS;
                                    win->SendBehind(nullptr);
                                    
                                    app_info currentActiveInfo;                           
                                    if (be_roster && be_roster->GetActiveAppInfo(&currentActiveInfo) == B_OK) {
                                        be_roster->ActivateApp(currentActiveInfo.team);
                                    }
                                }
                            }
                           
                            win->SetFlags(flags);
                            win->Unlock();
                            break;
                        }
                    }
                    be_app->Unlock();
                }
            } 
            needsRender = true;
        }

        // =========================================================================
        // AUTO-HIDE TRIGGER EVALUATOR
        // =========================================================================
        if (autoHideEnabled) {
            if (cursorIsInsideDock) {
                if (!hidingSettled) {
                    targetY = 0.0f; 
                    if (dockState == STATE_HIDDEN || dockState == STATE_HIDING) {
                        dockState = STATE_SHOWING;
                    }
                }
            } else {
                targetY = static_cast<float>(dockPanelH - sensorHeight); 
                hidingSettled = false; 
                if (dockState == STATE_VISIBLE || dockState == STATE_SHOWING) {
                    dockState = STATE_HIDING;
                }
            }
        } else {
            targetY = 0.0f;
            dockState = STATE_VISIBLE;
            hidingSettled = false;
        }


        // =========================================================================
        // TIMING DELTA CALCULATIONS (SMOOTHING MULTIPLIER)
        // =========================================================================
        Uint64 currentPerfTime = SDL_GetPerformanceCounter();
        float deltaTime = static_cast<float>(currentPerfTime - lastPerfTime) / static_cast<float>(perfFrequency);
        lastPerfTime = currentPerfTime;

        if (deltaTime > 0.1f) deltaTime = 0.1f; 

        float smoothingMultiplier = 1.0f - std::exp(-baseAnimationSpeed * deltaTime);

        if (std::abs(currentY - targetY) > 0.1f) {
            currentY += (targetY - currentY) * smoothingMultiplier;
            needsRender = true; 
        } else {
            currentY = targetY;
            if (autoHideEnabled) {
                if (dockState == STATE_SHOWING) dockState = STATE_VISIBLE;
                if (dockState == STATE_HIDING) {
                    dockState = STATE_HIDDEN;
                    hidingSettled = true; 
                }
            }
        }

        // =========================================================================
        // CPU-OPTIMIZED RENDER INJECTION
        // =========================================================================
        static int lastSentX = -1;
        static int lastSentY = -1;
        static uint32 lastSentButtons = 0;

        int hiddenScreenOffset = screenHeight - dockPanelH;
        int adjustedMouseY = localMouseY + hiddenScreenOffset;

        if (!cursorIsInsideDock) {
            if (lastSentX != -1 || lastSentY != -1) {
                desktopEngine.HandleMouseInput(localMouseX, adjustedMouseY, 0);
                needsRender = true;
                
                lastSentX = -1;
                lastSentY = -1;
                lastSentButtons = 0;
            }
        } else {
            if (localMouseX != lastSentX || adjustedMouseY != lastSentY || nativeButtons != lastSentButtons) {
                desktopEngine.HandleMouseInput(localMouseX, adjustedMouseY, nativeButtons);
                needsRender = true;

                lastSentX = localMouseX;
                lastSentY = adjustedMouseY;
                lastSentButtons = nativeButtons;
            }
        }

        uint32 currentTime = SDL_GetTicks();

        if (currentTime - lastMetricsUpdateTime >= 1000 || lastMetricsUpdateTime == 0) {
            lastMetricsUpdateTime = currentTime;
            needsRender = true; 
        }

        if (currentTime - lastRosterScanTime >= 400 || lastRosterScanTime == 0) {
            desktopEngine.SyncDockWithRunningDeskbarApps();
            lastRosterScanTime = currentTime;
            needsRender = true; 
        }

        if (needsRender) {
            // =========================================================================
            // DYNAMIC HARDWARE WINDOW RESIZING ENGINE
            // =========================================================================
            // FIX: Access the global fBaseIconSize variable directly
            float liveIconSize = fBaseIconSize; 
            if (liveIconSize <= 0.0f) liveIconSize = 48.0f; // Fail-safe default

            // Apply your dynamic scaling height equation
            int targetWindowHeight = static_cast<int>(std::ceil(liveIconSize * 2.0f + 50.0f)); 
            int targetWindowWidth  = screenWidth;
            int targetWindowY      = screenHeight - targetWindowHeight;

            // Track the size across frames so we don't spam the OS window manager
            static int lastSetH = -1;
            static int lastSetY = -1;

            if (targetWindowHeight != lastSetH || targetWindowY != lastSetY) {
                SDL_SetWindowSize(window, targetWindowWidth, targetWindowHeight);
                SDL_SetWindowPosition(window, 0, targetWindowY);
                
                glViewport(0, 0, targetWindowWidth, targetWindowHeight);
                
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                float panelTopY    = static_cast<float>(screenHeight - targetWindowHeight);
                float panelBottomY = static_cast<float>(screenHeight);
                gluOrtho2D(0.0, static_cast<float>(screenWidth), panelBottomY, panelTopY);    
                glMatrixMode(GL_MODELVIEW);
                
                dockPanelH = targetWindowHeight;
                lastSetH = targetWindowHeight;
                lastSetY = targetWindowY;
            }
            // =========================================================================

            desktopEngine.RenderFrame(currentY);   
            SDL_GL_SwapWindow(window);
            needsRender = false; 
        }



    }

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
