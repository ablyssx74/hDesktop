/*
 * Copyright 2026, Kris Beazley hDesktop@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */ 

#include <algorithm>
#include <AppKit.h>
#include <AppServerLink.h> 
#include <Bitmap.h>
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
#include <MenuItem.h>
#include <Message.h>
#include <Node.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <ParameterWeb.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>
#include <set>
#include <stdio.h>
#include <StorageKit.h>
#include <String.h>
#include <string>
#include <SupportKit.h> 
#include <TranslationUtils.h>
#include <vector>
#include <View.h>
#include <Window.h>
#include <NavMenu.h> 


class HaikuGlDesktopEngine;
class HaikuAppDrawerWindow; 
HaikuAppDrawerWindow* gActiveDrawerInstance = nullptr; 
BWindow* gActiveConfigInstance = nullptr; 
std::set<std::string> gFavoritePaths; 
bool autoHideEnabled; 
bool showSystemTray; 
bool dockAlwaysOnTop;
void SaveConfiguration(); 

enum {
	SDL_EVENT_WALLPAPER_CHANGED = SDL_USEREVENT + 1,
    MSG_AUTOHIDE_TOGGLED   = 'ahtg',
    MSG_SYSTEMTRAY_TOGGLED = 'sttg',
    MSG_AUTORAISE_TOGGLED  = 'srdt' 
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
};

struct DesktopIconItem {
    std::string name;
    HaikuTexture texture;       // Core 48x48 icon file asset
    HaikuTexture textTexture;   // Dynamic text string label texture
    HaikuRect bounds;
    HaikuRect textBounds;       // Layout boundaries for label text box below the icon
    bool isFolder;
};

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
        std::cout << "[Systray Matrix] Layout change caught! Updating texture nodes..." << std::endl;

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
public:
    ConfigView(BRect frame) : BView(frame, "ConfigView", B_FOLLOW_ALL, B_WILL_DRAW) {
        SetViewColor(rgb_color{24, 24, 28, 255});

        // Row 1: Dropped down slightly to account for the tighter shutdown button
        BRect checkboxRect(25.0f, 105.0f, frame.Width() - 25.0f, 120.0f);
        fAutoHideCheckbox = new BCheckBox(checkboxRect, "auto_hide_cb", "Enable Auto-Hide Dock", 
            new BMessage(MSG_AUTOHIDE_TOGGLED));
        fAutoHideCheckbox->SetHighColor(rgb_color{220, 225, 235, 255});
        fAutoHideCheckbox->SetValue(autoHideEnabled ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fAutoHideCheckbox);

        // Row 2: Placed tightly under Row 1
        BRect trayCheckboxRect(25.0f, 125.0f, frame.Width() - 25.0f, 140.0f);
        fSystemTrayCheckbox = new BCheckBox(trayCheckboxRect, "sys_tray_cb", "Show System Tray in Dock", 
            new BMessage(MSG_SYSTEMTRAY_TOGGLED));
        fSystemTrayCheckbox->SetHighColor(rgb_color{220, 225, 235, 255});
        fSystemTrayCheckbox->SetValue(showSystemTray ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fSystemTrayCheckbox);

        // Row 3: Placed tightly under Row 2
        BRect autoRaiseRect(25.0f, 145.0f, frame.Width() - 25.0f, 160.0f);
        fAutoRaiseCheckbox = new BCheckBox(autoRaiseRect, "auto_raise_cb", "Enable Dock Auto-raise", 
            new BMessage(MSG_AUTORAISE_TOGGLED));
        fAutoRaiseCheckbox->SetHighColor(rgb_color{220, 225, 235, 255});
        fAutoRaiseCheckbox->SetValue(dockAlwaysOnTop ? B_CONTROL_ON : B_CONTROL_OFF);
        AddChild(fAutoRaiseCheckbox);
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

        // 3. Define the Interactive "Shutdown App" Button Metrics (COMPRESSED)
        // Shrunk vertical footprint from 35px down to 24px (Y: 50 to 74)
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
        BString shutdownText("Shutdown App");
        float shutdownTextW = StringWidth(shutdownText.String());
        DrawString(shutdownText.String(), BPoint(shutdownBtnRect.left + (shutdownBtnRect.Width() - shutdownTextW) / 2.0f, 66.0f));

        // 4. Standard Window Control "Close" button tracking metrics at footer (RAISED)
        // Shifted upward into the clear view window zone (Y: 175 to 200)
        BRect closeBtnRect((canvasWidth - 100.0f) / 2.0f, 175.0f, 
                           (canvasWidth + 100.0f) / 2.0f, 200.0f);
        
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
        DrawString(btnText.String(), BPoint((canvasWidth - btnTextW) / 2.0f, 192.0f));
    }


    virtual void MouseMoved(BPoint point, uint32 transit, const BMessage* message) {
        Invalidate(); 
    }

    virtual void MouseDown(BPoint point) {
        float canvasWidth = Bounds().Width();
        
        //  Coordinates match the new compressed 24px tall button (Y: 50 to 74)
        BRect shutdownBtnRect(25.0f, 50.0f, canvasWidth - 25.0f, 74.0f);

        //  Coordinates match the newly raised close button position (Y: 175 to 200)
        BRect closeBtnRect((canvasWidth - 100.0f) / 2.0f, 175.0f, 
                           (canvasWidth + 100.0f) / 2.0f, 200.0f);
        
        if (shutdownBtnRect.Contains(point)) {
            if (be_app) {
                be_app->PostMessage(B_QUIT_REQUESTED);
            }
            return;
        }

        if (closeBtnRect.Contains(point)) {
            if (Window()) {
                Window()->Quit(); 
            }
            return;
        }
    }

    
    virtual void AttachedToWindow() {
        BView::AttachedToWindow();
        fAutoHideCheckbox->SetTarget(this);
        fSystemTrayCheckbox->SetTarget(this);
        fAutoRaiseCheckbox->SetTarget(this);
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
        : BWindow(BRect(0, 0, 420, 220), "hdesktop Configuration",
                  B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, 
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE) {
        
        // Center the layout frame inside the screen space coordinates of the active app drawer
        float targetX = centralAnchor.left + (centralAnchor.Width() - 420.0f) / 2.0f;
        float targetY = centralAnchor.top + (centralAnchor.Height() - 220.0f) / 2.0f;
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
	                std::cout << "[hdesktop] Close shortcut pressed. Dismissing app drawer container." << std::endl;
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
        BRect rebootSysRect(120.0f, 45.0f, 190.0f, 76.0f);



        // Fetch active cursor coordinates for interface tracking
        BPoint cursorPoint;
        uint32 transitButtons;
        GetMouse(&cursorPoint, &transitButtons, false);

        SetDrawingMode(B_OP_ALPHA);

        // 3. Draw Exit Button (Left Side)
        if (exitIconRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{230, 75, 75, 45}); // Soft red hover glow
            FillRect(exitIconRect.InsetBySelf(-4, -4));
            SetHighColor(rgb_color{255, 90, 90, 255});
        } else {
            SetHighColor(rgb_color{200, 70, 70, 220}); // Muted red vector
        }
        StrokeLine(BPoint(exitIconRect.left, exitIconRect.top), BPoint(exitIconRect.right, exitIconRect.bottom));
        StrokeLine(BPoint(exitIconRect.left, exitIconRect.bottom), BPoint(exitIconRect.right, exitIconRect.top));
        
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
        BString shutText("Shutdown");
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

        BString rebootText("Reboot");
        float rebootTextW = StringWidth(rebootText.String());
        DrawString(rebootText.String(), BPoint(rebootSysRect.left + (rebootSysRect.Width() - rebootTextW) / 2.0f, 64.0f));


        // 4. Draw Config Gear Button (Right Side)
        if (configIconRect.Contains(cursorPoint)) {
            SetHighColor(rgb_color{100, 120, 160, 50}); // Slate hover glow
            FillRect(configIconRect.InsetBySelf(-4, -4));
            SetHighColor(rgb_color{150, 180, 230, 255});
        } else {
            SetHighColor(rgb_color{130, 140, 160, 200}); // Muted slate vector
        }
        StrokeRect(configIconRect);
        StrokeEllipse(configIconRect.InsetByCopy(6, 6));


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
		                    std::cout << "[hdesktop] Removed from Favorites: " << pathStr << std::endl;
		                } else {
		                    gFavoritePaths.insert(targetKey);
		                    std::cout << "[hdesktop] Added to Favorites: " << pathStr << std::endl;
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
		                // Multiplier to increase scrolling speed (adjust 15.0f to taste)
		                float scrollSpeedMultiplier = 15.0f; 
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

        // 1. Process Exit Button Trigger Click
        if (exitIconRect.Contains(point)) {
            // ... rest of your exit code

            std::cout << "[Native Drawer] Exit Action Intercepted. Closing popup container." << std::endl;
            if (Window()) {
                Window()->Quit(); 
            }
            return;
        }


        // --- ADDED: INTERCEPT SHUTDOWN SYSTEM TRACKER ---
        if (shutdownSysRect.Contains(point)) {
            std::cout << "[hdesktop] Invoking Native CLI System Power Down." << std::endl;
            
            // Runs a standard safe background power down sequence.
            // Replace "shutdown" with "shutdown -q" if you want to bypass alerts completely.
            std::system("shutdown &");
            return;
        }

        // --- ADDED: INTERCEPT REBOOT SYSTEM TRACKER ---
        if (rebootSysRect.Contains(point)) {
            std::cout << "[hdesktop] Invoking Native CLI System Restart." << std::endl;
            
            // Runs a standard safe system reboot. 
            // The "-r" flag explicitly tells Haiku OS to perform a machine reboot.
            std::system("shutdown -r &");
            return;
        }


        // 2. Process Configuration Button Trigger Click
        if (configIconRect.Contains(point)) {
            std::cout << "[Native Drawer] Config Menu Click Hook Executed." << std::endl;
            
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
public:
    HaikuAppDrawerWindow(float screenH)
        : BWindow(BRect(0, 0, 100, 100), "App Dashboard Menu",
                  B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL, 
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_CLOSE_ON_ESCAPE) {
        
        // DYNAMIC SCREEN WIDTH AUTODETECTION PIPELINE
        BScreen activeScreen(this);
        BRect screenFrame = activeScreen.Frame();
        
        // Leave a uniform margin border frame surrounding the display dashboard block
        float horizontalMarginGap = 40.0f;
        float targetPanelWidth = screenFrame.Width() - (horizontalMarginGap * 2.0f);
        float targetPanelHeight = screenH - 200.0f; // Leaves clean layout room for your dock plate strip below

        // Center placement across horizontal monitor properties and position upper coordinate
        MoveTo(horizontalMarginGap, 30.0f);
        ResizeTo(targetPanelWidth, targetPanelHeight);

        BRect bounds = Bounds();
        // Shift scrollbar channel footprint off the outer border edge cleanly
        bounds.right -= B_V_SCROLL_BAR_WIDTH; 

        DrawerView* drawerView = new DrawerView(bounds);
        BScrollView* scrollView = new BScrollView("DashboardScroll", drawerView, 
            B_FOLLOW_ALL, 0, false, true, B_NO_BORDER); // Continuous mouse wheel tracks seamlessly here
            
        AddChild(scrollView);
    }
    
    virtual ~HaikuAppDrawerWindow() {
        gActiveDrawerInstance = nullptr;
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
	    std::cout << "[Engine] Reloading wallpaper graphics assets..." << std::endl;
	
	    // Use the inner integer identifier field for the OpenGL cleanup
	    if (fWallpaperTexture.id != 0) {
	        glDeleteTextures(1, &fWallpaperTexture.id);
	        fWallpaperTexture.id = 0;
	    }
	
	    BString capturedWallpaper = GetActiveHaikuWallpaperPath();
	    std::cout << "[Engine] Detected new wallpaper target: " << capturedWallpaper.String() << std::endl;
	
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
        
        std::cout << "[Tracker window] Scrolled canvas position offset: " << fScrollOffset << std::endl;
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
	            
	            // --- MODIFIED EXCLUSIVE DUPLICATE FILTER FOR ICEWEASEL ONLY ---
	            // =================================================================
	            bool isDuplicate = false;
	            if (appSignature == "application/x-vnd.iceweasel") {
	                for (const auto& sig : processedSignatures) {
	                    if (sig == appSignature) {
	                        isDuplicate = true; 
	                        break;
	                    }
	                }
	            }
	            if (isDuplicate) continue; // Skip subsequent Iceweasel teams, allow all other apps
	
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
	    
	    float anchoredMenuY = static_cast<float>(args->winY) - 5.0f;
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
                                std::cout << "[Systray Menu] Toggle Match: Dismissing menu canvas cleanly." << std::endl;
                                fLastTrackerMenuCloseTime = 0; 
                                return; 
                            }

                            // Reused active menu latch check
                            if (fTrackerMenuIsActive) {
                                std::cout << "[Systray Menu] Active Close Match: Letting menu close naturally." << std::endl;
                                return; 
                            }

                            std::cout << "[Systray Menu] Offloading replicant query to background thread: " << item.name << std::endl;
                            fTrackerMenuIsActive = true; 

                            int32 winX = 0, winY = 0;
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
                                            std::cout << "    [Profile] Replicant uses private layout specs. Injecting native system actions..." << std::endl;
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
		                    std::cout << "[Dock Input] Launching control panel for tray applet: " << item.name << std::endl;
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
	    // DOCK GEOMETRY & LAYER PRE-CALCULATIONS (EXACT MATCH FOR RENDERFRAME)
	    // =========================================================================

	    float baseSize = 48.0f;
	    float padding = 12.0f;
	    
	    size_t baselineLaunchersCount = fDesktopItems.size() + 1; 

	    size_t activeWindowsCount = 0;
	    for (const auto& w : fTaskbarWindows) {
	        if (*w.openStateFlag == true) activeWindowsCount++;
	    }

	    size_t totalIconsCount = baselineLaunchersCount + activeWindowsCount;

	    // Configuration variables matching RenderFrame exactly
	    float clockSectionPadding = 24.0f;
	    float cpuGraphWidth = 60.0f;
	    float separatorGapPadding = 16.0f;
	    float baseTrashSize = 48.0f;
	    float baseVolumeWidth = 44.0f; 

	    std::vector<float> dynamicWidths;
	    std::vector<float> dynamicScales;
	    float maxDockHeight = baseSize;

	    // -------------------------------------------------------------------------
	    // PASS 1: PROGRESSIVE MULTI-PASS COORDINATE RE-ANCHORING (CONVERGENCE SYNC)
	    // -------------------------------------------------------------------------
	    float totalCalculatedWidth = 0.0f; 
	    
	    for (int convergencePass = 0; convergencePass < 3; ++convergencePass) {
	        dynamicWidths.clear();
	        dynamicScales.clear();
	        maxDockHeight = baseSize;
	        
	        // Start reading layouts from a relative left offset margin
	        float progressiveX = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
	        
	        // 1. Process standard app launchers & active window indicators
	        for (size_t i = 0; i < totalIconsCount; ++i) {
	            float approxCenterX = progressiveX + (baseSize / 2.0f);
	            
			float distanceX = std::abs(x - approxCenterX); 
			
			float scale = 1.0f;
			if (y >= (fHeight - 140.0f) && distanceX < 160.0f) { 
 
	                float ratio = distanceX / 160.0f;
	                scale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	            }
	
	            float finalSize = baseSize * scale;
	            dynamicWidths.push_back(finalSize);
	            dynamicScales.push_back(scale);
	            
	            if (finalSize > maxDockHeight) maxDockHeight = finalSize;
	            progressiveX += finalSize + padding;
	        }
	        if (totalIconsCount > 0) progressiveX -= padding; 
	
	        // Account for the structural native app split divider (FIXED DE-DUPLICATION)
	        if (activeWindowsCount > 0) {
	            progressiveX += separatorGapPadding;
	        }
			
			// Process Haiku Trash Can Component Metrics
	        progressiveX += clockSectionPadding;
	        float approxTrashCenterX = progressiveX + (baseTrashSize / 2.0f);
	        float distanceTrashX = std::abs(fMouseX - approxTrashCenterX);
	        
	        float trashScale = 1.0f;
	        if (fMouseY >= (fHeight - 140.0f) && distanceTrashX < 160.0f) {
	            float ratio = distanceTrashX / 160.0f;
	            trashScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        
	        float finalTrashSize = baseTrashSize * trashScale;
	        dynamicWidths.push_back(finalTrashSize);
	        dynamicScales.push_back(trashScale);
	        if (finalTrashSize > maxDockHeight) maxDockHeight = finalTrashSize;
	        progressiveX += finalTrashSize;

	        // =========================================================================
	        // DYNAMIC SYSTEM TRAY SLOT WIDTH PARAMETER (NEW DYNAMIC CODE)
	        // NOTE: Uses 6.0f internal spacing to match your main RenderFrame pipeline!
	        // =========================================================================
	        float traySectionPadding = clockSectionPadding;
	        size_t trayCount = fLiveTrayItems.size();
	        
            float baselineTrayWidth = 0.0f;
            if (showSystemTray && trayCount > 0) {
                baselineTrayWidth = (trayCount * 16.0f) + ((trayCount > 1 ? trayCount - 1 : 0) * 6.0f);
            }
	
	        progressiveX += traySectionPadding;
	        float approxTrayCenterX = progressiveX + (baselineTrayWidth / 2.0f);
	        float distanceTrayX = std::abs(fMouseX - approxTrayCenterX);
	        
	        float trayScale = 1.0f;
	        if (fMouseY >= (fHeight - 140.0f) && distanceTrayX < 160.0f) {
	            float ratio = distanceTrayX / 160.0f;
	            trayScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        
	        dynamicWidths.push_back(baselineTrayWidth * trayScale);
	        dynamicScales.push_back(trayScale);
	        progressiveX += (baselineTrayWidth * trayScale);
	        // =========================================================================
	        
	
	        // Process System Clock Component Metrics
	        if (fClockTexture.id != 0) {
	            progressiveX += clockSectionPadding;             
	            float highDpiCompensateFactor = 0.42f;
	            float baselineClockLayoutWidth = static_cast<float>(fClockWidth) * highDpiCompensateFactor;              
	            float approxClockCenterX = progressiveX + (baselineClockLayoutWidth / 2.0f);                
	            float distanceClockX = std::abs(fMouseX - approxClockCenterX);               
	            float clockScale = 1.0f;
	            
	            if (fMouseY >= (fHeight - 140.0f) && distanceClockX < 160.0f) {
	                float ratio = distanceClockX / 160.0f;
	                clockScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	            }
	            
	            dynamicWidths.push_back(baselineClockLayoutWidth * clockScale);
	            dynamicScales.push_back(clockScale);
	            progressiveX += (baselineClockLayoutWidth * clockScale);
	        } else {
	            dynamicWidths.push_back(0.0f);
	            dynamicScales.push_back(1.0f);
	        }
	
	        // Process Dynamic Volume Slider Component Metrics
	        progressiveX += clockSectionPadding;
	        float approxVolCenterX = progressiveX + (baseVolumeWidth / 2.0f);
	        float distanceVolX = std::abs(fMouseX - approxVolCenterX);
	        
	        float volScale = 1.0f;
	        if (fMouseY >= (fHeight - 140.0f) && distanceVolX < 160.0f) {
	            float ratio = distanceVolX / 160.0f;
	            volScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        dynamicWidths.push_back(baseVolumeWidth * volScale);
	        dynamicScales.push_back(volScale);
	        progressiveX += (baseVolumeWidth * volScale);
	

	        // 5. Process Graphical CPU Monitor Metrics
	        progressiveX += clockSectionPadding; 
	        float approxCpuCenterX = progressiveX + (cpuGraphWidth / 2.0f);
	        float distanceCpuX = std::abs(fMouseX - approxCpuCenterX);
	        
	        float cpuScale = 1.0f;
	        if (fMouseY >= (fHeight - 140.0f) && distanceCpuX < 160.0f) {
	            float ratio = distanceCpuX / 160.0f;
	            cpuScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	        
	        float finalCpuWidth = cpuGraphWidth * cpuScale;
	        dynamicWidths.push_back(finalCpuWidth);
	        dynamicScales.push_back(cpuScale);
	        progressiveX += finalCpuWidth;
	
	        // Save converged dimensions to update anchor points on the next pass
	        float leftEdge = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
	        totalCalculatedWidth = progressiveX - leftEdge;
	    }


	    // -------------------------------------------------------------------------
	    // PASS 2: BOUNDS SETTLEMENT AND BACKPLATE GEOMETRY ALLOCATION
	    // -------------------------------------------------------------------------
	    size_t trashSlotIdx   = totalIconsCount;
	    size_t traySlotIdx  = totalIconsCount + 1;
	    size_t clockSlotIdx  = totalIconsCount + 2;
	    size_t volumeSlotIdx = totalIconsCount + 3;
	    size_t cpuSlotIdx    = totalIconsCount + 4;
	    

	    float dockMarginBottom = 15.0f;
	    HaikuRect dockPlate;
	    float outerPlatePadding = 40.0f; 
	    dockPlate.left = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f) - (outerPlatePadding / 2.0f);
	    dockPlate.right = (fWidth / 2.0f) + (totalCalculatedWidth / 2.0f) + (outerPlatePadding / 2.0f);
	    dockPlate.bottom = fHeight - dockMarginBottom;
	    dockPlate.top = dockPlate.bottom - maxDockHeight - 20.0f;

	    // Lock down definitive trash hitbox using the converged stream variables
	    float renderingTrashSize = dynamicWidths[trashSlotIdx];
	    
	    // REPLACE WITH THIS TO CALIBRATE ALIGNMENT:
		float layoutTrackerX = dockPlate.left + 20.0f;

	    for (size_t idx = 0; idx < totalIconsCount; ++idx) {
	        layoutTrackerX += dynamicWidths[idx] + padding;
	    }
	    if (totalIconsCount > 0) layoutTrackerX -= padding;
	    if (activeWindowsCount > 0) layoutTrackerX += separatorGapPadding;
	    
	    // SYNCHRONIZED: Account for the horizontal width footprint of the new System Tray slot!
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

	    float currentX = dockPlate.left + 20.0f; // Exact rendering origin used in Section 5
	    size_t evaluationSlotIdx = 0;
	
	    // STEP A: EVALUATE BASELINE SYSTEM LAUNCHERS (MENU LEAF + FILE SHORTCUTS)
	    for (size_t i = 0; i < baselineLaunchersCount; ++i) {
	        float size = dynamicWidths[evaluationSlotIdx];
	        HaikuRect realIconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
	
	        if (x >= realIconBounds.left && x <= realIconBounds.right &&
	            y >= realIconBounds.top  && y <= realIconBounds.bottom) {
	            
            if (i == 0) {
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
	            // FIX: Explicitly exclude SDL_BUTTON_RIGHT (3) to prevent accidental closes!
	            if (button == SDL_BUTTON_MIDDLE && button != SDL_BUTTON_RIGHT && !isTracker) {
	                std::cout << "[SYSTEM ACTION] ---> Closing App cleanly via BMessenger. Team ID: " << activeTaskWin.teamId << std::endl;
	                
	                BMessenger targetAppMessenger(NULL, activeTaskWin.teamId);
	                if (targetAppMessenger.IsValid()) {
	                    // Standard graceful exit request handled natively by the app loop thread
	                    targetAppMessenger.SendMessage(B_QUIT_REQUESTED);
	                } else {
	                    std::cout << "   [Fallback] Messenger invalid. Issuing kernel kill_team to free thread structures." << std::endl;
	                    // Native Haiku OS kernel primitive to cleanly reclaim memory allocations from non-responsive teams
	                    kill_team(activeTaskWin.teamId);
	                }
	                
	                fShowMainMenu = false;
	                std::cout << "========================================\n" << std::endl;
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
				            std::cout << "[Tracker Menu] Toggle Match: Dismissing menu canvas cleanly on second click." << std::endl;
				            fLastTrackerMenuCloseTime = 0; 
				            return; 
				        }
				    
				        // ACTIVE MENU CLOSE CHECK: If the menu is currently visible and they click again, close it
				        if (fTrackerMenuIsActive) {
				            std::cout << "[Tracker Menu] Active Close Match: Intercepting click to let menu close naturally." << std::endl;
				            return; 
				        }
				    
				        std::cout << "[Tracker Menu] Offloading file navigator to background thread..." << std::endl;
				        fTrackerMenuIsActive = true; // Engage active state safety latch
				    
				        int32 winX = 0, winY = 0;
				        SDL_Window* activeWin = SDL_GetMouseFocus();
				        if (activeWin) {
				            SDL_GetWindowPosition(activeWin, &winX, &winY);
				        }
				    
				        TrackerMenuArgs* args = new TrackerMenuArgs();
				        args->engine = this; // Pass engine instance pointer safely
				        args->winX = winX;
				        args->winY = winY;
				        args->mouseX = x; 
				    
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
				    
				    // Left-clicks (or other mouse inputs) fall completely through this block!
				    // They will land right into your AppServerLink pipeline codes below,
				    // minimizing or restoring your open desktop/folder panels naturally.
				}

				
			
		        // -----------------------------------------------------------
					
		        // GENERAL APPLICATION BEHAVIOR (NON-TRACKER APPS)
	            #ifndef AS_MINIMIZE_TEAM
	            #define AS_MINIMIZE_TEAM 5
	            #endif
	            #ifndef AS_BRING_TEAM_TO_FRONT
	            #define AS_BRING_TEAM_TO_FRONT 6
	            #endif
	
	            if (activeTaskWin.isMinimized == false) {
	                std::cout << "[APP_SERVER ROUTING] ---> Action: MINIMIZE TEAM VIA Opcodes Pipeline" << std::endl;
	                
	                // Establish direct connection straight to the central window management loop
	                BPrivate::AppServerLink link;
	                link.StartMessage(AS_MINIMIZE_TEAM);
	                link.Attach<team_id>(activeTaskWin.teamId);
	                link.Flush();
	                
	                // Safely drop window workspace focus via native roster management
	                be_roster->ActivateApp(-1);
	                activeTaskWin.isMinimized = true; 
	            } 
	            else {
	                std::cout << "[APP_SERVER ROUTING] ---> Action: RESTORE TEAM VIA Opcodes Pipeline" << std::endl;
	                
	                // First, pull the windows into active workspace focus pools natively
	                app_info targetAppInfo;
	                if (be_roster->GetRunningAppInfo(activeTaskWin.teamId, &targetAppInfo) == B_OK) {
	                    be_roster->ActivateApp(targetAppInfo.team);
	                } else {
	                    be_roster->ActivateApp(activeTaskWin.teamId);
	                }
	                
	                // Open low-level connection to bring every window surface frame to foreground
	                BPrivate::AppServerLink link;
	                link.StartMessage(AS_BRING_TEAM_TO_FRONT);
	                link.Attach<team_id>(activeTaskWin.teamId);
	                link.Flush();
	                
	                activeTaskWin.isMinimized = false; 
	            }
	            
	            std::cout << "========================================\n" << std::endl;
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
	                std::cout << "[Trash Menu] Toggle Match: Dismissing menu canvas cleanly." << std::endl;
	                fLastTrackerMenuCloseTime = 0; 
	                return; 
	            }
	
	            // Reused active menu latch check
	            if (fTrackerMenuIsActive) {
	                std::cout << "[Trash Menu] Active Close Match: Letting menu close naturally." << std::endl;
	                return; 
	            }
	
	            std::cout << "[Trash Menu] Offloading trash context menu to background thread..." << std::endl;
	            fTrackerMenuIsActive = true; 
	
	            int32 winX = 0, winY = 0;
	            SDL_Window* activeWin = SDL_GetMouseFocus();
	            if (activeWin) {
	                SDL_GetWindowPosition(activeWin, &winX, &winY);
	            }
	
	            TrackerMenuArgs* args = new TrackerMenuArgs();
	            args->engine = this; 
	            args->winX = winX;
	            args->winY = winY;
	            args->mouseX = x; 
	            args->mouseY = y; 
	
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
	                
	                float anchoredMenuY = static_cast<float>(threadArgs->winY) - 5.0f; 
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
	                        
	                        // CASE 1: RIGHT-CLICK -> PARSE AND PRESENT REAL CONTEXT DROPDOWNS
	                        if (button == SDL_BUTTON_RIGHT) {
	                            std::cout << "[Dock Input] Querying serialized menu archive tree for: " << fLiveTrayItems[t].name << std::endl;
	
	                            BMessage menuRequest(B_GET_PROPERTY);
	                            menuRequest.AddSpecifier("Menu");
	                            menuRequest.AddSpecifier("Replicant", fLiveTrayItems[t].name.c_str());
	                            menuRequest.AddSpecifier("View", "Status");
	                            menuRequest.AddSpecifier("View", "Deskbar");
	
	                            BMessage menuReply;
	                            if (deskbarMessenger.SendMessage(&menuRequest, &menuReply) == B_OK) {
	                                BPopUpMenu* localMenu = new BPopUpMenu("SystrayContext", false, false);
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
	
	                                // Structural fail-safe triggers
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
	
	                                int32 winX = 0, winY = 0;
	                                SDL_Window* activeWin = SDL_GetMouseFocus();
	                                if (activeWin) SDL_GetWindowPosition(activeWin, &winX, &winY);
	                                BPoint screenClickPoint(static_cast<float>(winX + x), static_cast<float>(winY + y));
	
	                                BMenuItem* chosenItem = localMenu->Go(screenClickPoint);
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
	                        } 
	                        // CASE 2: LEFT-CLICK -> PANEL SHORTCUT LAUNCHERS
	                        else if (button == SDL_BUTTON_LEFT) {
	                            std::cout << "[SYSTEM TRAY ACTION] ---> Launching panel for: " << fLiveTrayItems[t].name << std::endl;
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
	    if (fClockTexture.id != 0) {
	        float dynamicClockW = dynamicWidths[clockSlotIdx];
	        currentX += clockSectionPadding;
	        
	        HaikuRect clockBounds = { currentX, dockPlate.top, currentX + dynamicClockW, dockPlate.bottom };
	        if (x >= clockBounds.left && x <= clockBounds.right && y >= clockBounds.top && y <= clockBounds.bottom) {
	            std::system("/boot/system/preferences/Time &"); 
	            return;
	        }
	        currentX += dynamicClockW;
	    }
	
	    // -------------------------------------------------------------------------
	    // Skip past Volume Slider component space footprint natively 
	    // -------------------------------------------------------------------------
	    currentX += clockSectionPadding + dynamicWidths[volumeSlotIdx];
	

	
	    // -------------------------------------------------------------------------
	    // Evaluate Click Bounds for Graphical LED CPU Monitor Component
	    // -------------------------------------------------------------------------
	    currentX += clockSectionPadding;
	    float dynamicGraphWidth = dynamicWidths[cpuSlotIdx];
	    HaikuRect cpuBounds = { currentX, dockPlate.top, currentX + dynamicGraphWidth, dockPlate.bottom };
	    
	    if (x >= cpuBounds.left && x <= cpuBounds.right && y >= cpuBounds.top && y <= cpuBounds.bottom) {
	        // Fetch absolute screen coordinate offsets via the active window context
	        int32 winX = 0, winY = 0;
	        SDL_Window* activeWin = SDL_GetMouseFocus();
	        if (activeWin) {
	            SDL_GetWindowPosition(activeWin, &winX, &winY);
	        }
	
	        // ACTIVE MENU CLOSE CHECK: Let the menu naturally collapse if they click again
	        if (fCpuMenuIsActive) {
	            std::cout << "[CPU Monitor] Active Intercept: Letting menu canvas close cleanly." << std::endl;
	            return; 
	        }
	
	        if (button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT) {
	            std::cout << "[CPU Monitor] Offloading ProcessController menus to async window looper..." << std::endl;
	            fCpuMenuIsActive = true; // Engage active state safety shield latch
	
	            // Bundle coordinates to pass safely across the memory barrier
	            CpuMenuArgs* args = new CpuMenuArgs();
	            args->engine = this;
	            args->winX = winX;
	            args->winY = winY;
	            args->mouseX = x;
	            args->mouseY = y;
	
	            // Spawns and detaches the menu runner immediately.
	            // Your core SDL graphics framework thread can now render frames uninterrupted.
	            new AsyncCpuMenuRunner(args);
	        }
	        return;
	    }
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




  void RenderFrame(float yOffset) {
        // =========================================================================
        // 1. STEP POSIX TIMING ENGINES AND KERNEL RECORD SAMPLES
        // =========================================================================
        SyncDockWithRunningDeskbarApps(); // Rebuilds fTaskbarWindows using real OS states!
        
        UpdateLiveClockTexture();
        UpdateGlobalCpuLoadTracker(); // Pull processor ticks and update our 40-cell buffer array

        glClearColor(fBgColorR, fBgColorG, fBgColorB, 1.0f);
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
        float baseSize = 48.0f;
        float padding = 12.0f;
        
        size_t baselineLaunchersCount = fDesktopItems.size() + 1; 

        size_t activeWindowsCount = 0;
        for (const auto& w : fTaskbarWindows) {
            if (*w.openStateFlag == true) activeWindowsCount++;
        }

        size_t totalIconsCount = baselineLaunchersCount + activeWindowsCount;

        // Configuration variables for the status widgets
        float clockSectionPadding = 24.0f;
        float cpuGraphWidth = 60.0f;
        float separatorGapPadding = 16.0f;
        float baseTrashSize = 48.0f;
        float baseVolumeWidth = 44.0f; // Slider horizontal layout width footprint allocation

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
            progressiveX += clockSectionPadding;
            float approxVolCenterX = progressiveX + (baseVolumeWidth / 2.0f);
            
            // Calculate spatial center point on the Y axis for the slider asset
            float approxVolCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceVolX = std::abs(fMouseX - approxVolCenterX);
            float distanceVolY = std::abs(fMouseY - approxVolCenterY);
            float distanceVol2D = std::sqrt(distanceVolX * distanceVolX + distanceVolY * distanceVolY);
            
            float volScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceVol2D < 180.0f) {
                float ratio = distanceVol2D / 180.0f;
                volScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            dynamicWidths.push_back(baseVolumeWidth * volScale);
            dynamicScales.push_back(volScale);
            progressiveX += (baseVolumeWidth * volScale);


            // =========================================================================
            // PROCESS GRAPHICAL CPU MONITOR METRICS (2D SMOOTH FIX)
            // =========================================================================
            progressiveX += clockSectionPadding; 
            float approxCpuCenterX = progressiveX + (cpuGraphWidth / 2.0f);
            
            // Calculate spatial center point on the Y axis for the processor chart cells
            float approxCpuCenterY = fHeight - 10.0f - (baseSize / 2.0f);
            
            float distanceCpuX = std::abs(fMouseX - approxCpuCenterX);
            float distanceCpuY = std::abs(fMouseY - approxCpuCenterY);
            float distanceCpu2D = std::sqrt(distanceCpuX * distanceCpuX + distanceCpuY * distanceCpuY);
            
            float cpuScale = 1.0f;
            if (fCursorIsInsideHitbox && distanceCpu2D < 180.0f) {
                float ratio = distanceCpu2D / 180.0f;
                cpuScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
            
            float finalCpuWidth = cpuGraphWidth * cpuScale;
            dynamicWidths.push_back(finalCpuWidth);
            dynamicScales.push_back(cpuScale);
            progressiveX += finalCpuWidth;

            // Save converged dimensions to update anchor points on the next pass
            float leftEdge = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f);
            totalCalculatedWidth = progressiveX - leftEdge;
        }


        // -------------------------------------------------------------------------
        // PASS 2: BOUNDS SETTLEMENT AND BACKPLATE GEOMETRY ALLOCATION
        // -------------------------------------------------------------------------
        // FIXED: Added local explicit type declarations for every tracking index
        size_t trashSlotIdx   = totalIconsCount;
        size_t traySlotIdx  = totalIconsCount + 1;
        size_t clockSlotIdx  = totalIconsCount + 2;
        size_t volumeSlotIdx = totalIconsCount + 3;
        size_t cpuSlotIdx    = totalIconsCount + 4;
        


        float dockMarginBottom = 15.0f;
        HaikuRect dockPlate;
        float outerPlatePadding = 40.0f; 
        dockPlate.left = (fWidth / 2.0f) - (totalCalculatedWidth / 2.0f) - (outerPlatePadding / 2.0f);
        dockPlate.right = (fWidth / 2.0f) + (totalCalculatedWidth / 2.0f) + (outerPlatePadding / 2.0f);
        dockPlate.bottom = fHeight - dockMarginBottom;
        dockPlate.top = dockPlate.bottom - maxDockHeight - 20.0f;

        // Lock down definitive trash hitbox using converged data fields
        float renderingTrashSize = dynamicWidths[trashSlotIdx];
        
        float layoutTrackerX = dockPlate.left + 20.0f;
        for (size_t idx = 0; idx < totalIconsCount; ++idx) {
            layoutTrackerX += dynamicWidths[idx] + padding;
        }
        if (totalIconsCount > 0) layoutTrackerX -= padding;
        if (activeWindowsCount > 0) layoutTrackerX += separatorGapPadding;
        
        // Add System Tray width first
        layoutTrackerX += clockSectionPadding + dynamicWidths[traySlotIdx];
        
        // Add Clock width second
        if (fClockTexture.id != 0) layoutTrackerX += clockSectionPadding + dynamicWidths[clockSlotIdx];
        
        // Add Volume slider width third
        layoutTrackerX += clockSectionPadding + dynamicWidths[volumeSlotIdx];

        
        layoutTrackerX += clockSectionPadding;
        fTrashRect.left = layoutTrackerX;
        fTrashRect.right = fTrashRect.left + renderingTrashSize;
        fTrashRect.top = dockPlate.bottom - 10.0f - renderingTrashSize;
        fTrashRect.bottom = dockPlate.bottom - 10.0f;


        // Render backplate container shelf
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float cornerRadius = 15.0f;
        DrawFilledRoundedRect(dockPlate, cornerRadius, 0.95f, 0.95f, 0.95f, 0.4f); 
        DrawOutlineRoundedRect(dockPlate, cornerRadius, 0.15f, 0.15f, 0.15f, 0.8f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);



        // =========================================================================
        // 5. CORE ICON RENDERING ENGINE WITH ACTIVE RUNNING TASKBAR SPLITS
        // =========================================================================
        float currentX = dockPlate.left + 20.0f;
        size_t renderingSlotIdx = 0;

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
		    
		    // FIX: Snap the line to a clean whole pixel boundary to prevent subpixel bleeding
		    float snappedX = std::floor(currentX + 0.5f); 

		    glLineWidth(2.0f); 
		    glColor4f(0.15f, 0.15f, 0.15f, 0.5f); // Matte Charcoal split divider
		    glBegin(GL_LINES);
		        glVertex2f(snappedX, dockPlate.top + 6.0f);
		        glVertex2f(snappedX, dockPlate.bottom - 6.0f);
		    glEnd();
		    
		    currentX += (separatorGapPadding / 2.0f) + padding;
		}

		
		// =========================================================================
		// STEP 3: RENDER THE LIVE RUNNING APPLICATION WINDOW CONTEXT TAIL LOG MODULES
		// =========================================================================
		for (size_t w = 0; w < fTaskbarWindows.size(); ++w) {
		    auto& activeTaskWin = fTaskbarWindows[w];		
		    float size = dynamicWidths[renderingSlotIdx];
		    HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
		

                 // GENERAL APPLICATIONS: Keep the fast thread execution checks 
                bool generalAppIsVisible = false;
                thread_info info;
                int32 cookie = 0;
                while (get_next_thread_info(activeTaskWin.teamId, &cookie, &info) == B_OK) {
                    BString threadName(info.name);
                    
                    if (info.state == B_THREAD_RUNNING || info.state == B_THREAD_READY) {
                        if (threadName.IFindFirst("window") >= 0 || 
                            threadName.IFindFirst("loop") >= 0 || 
                            threadName.IFindFirst("render") >= 0 || 
                            threadName.Compare(activeTaskWin.title.c_str()) == 0) {
                            generalAppIsVisible = true;
                            break;
                        }
                    }
                
                
                // Check system-wide foreground focus
                app_info activeAppInfo;
                bool isCurrentlyForeground = false;
                if (be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
                    if (activeAppInfo.team == activeTaskWin.teamId) {
                        isCurrentlyForeground = true;
                    }
                }

                // =========================================================================
                // MULTI-INSTANCE INSTANCE FIX: CREATE COMPOSITE STRUCTURAL KEYS FOR INDEPENDENT STREAKS
                // =========================================================================
                // If you named your internal sub-window variable windowIndex, id, or instanceId, 
                // match it here (assuming 'windowIndex' based on the tracking pipeline).
      
				std::pair<team_id, int32> instanceKey(activeTaskWin.teamId, static_cast<int32>(w));

                static std::map<std::pair<team_id, int32>, int32> invisibleStreak;
                static std::map<std::pair<team_id, int32>, int32> visibleStreak;
                // =========================================================================
                
                // ASYMMETRICAL BALANCING:
                const int32 THRESHOLD_MINIMIZE = 15; 
                const int32 THRESHOLD_MAXIMIZE = 33; 

                bool nextState = activeTaskWin.isMinimized;

                // FIX: Establish a temporal guard to allow newly woken app threads to stabilize 
                // before letting thread profiling override the click event state.
                static std::map<std::pair<team_id, int32>, uint32> lastStateChangeTicks;
                uint32 currentTicks = SDL_GetTicks();
                
                // Track if a manual state shift just occurred
                static std::map<std::pair<team_id, int32>, bool> lastKnownState;
                if (lastKnownState[instanceKey] != activeTaskWin.isMinimized) {
                    lastStateChangeTicks[instanceKey] = currentTicks;
                    lastKnownState[instanceKey] = activeTaskWin.isMinimized;
                }

                // Grace period: Lock the state to what the click handler set for 500ms
                bool inGracePeriod = (currentTicks - lastStateChangeTicks[instanceKey] < 500);

                if (isCurrentlyForeground) {
                    invisibleStreak[instanceKey] = 0;
                    visibleStreak[instanceKey] = 0;
                    nextState = false;
                } else if (!inGracePeriod) { // Only evaluate streaks if outside the grace period
                    if (!generalAppIsVisible) {
                        invisibleStreak[instanceKey]++;
                        visibleStreak[instanceKey] = 0;
                        
                        bool quickBackgroundCheck = false;
                        if (activeAppInfo.team == activeTaskWin.teamId) {
                            quickBackgroundCheck = (activeAppInfo.flags & B_BACKGROUND_APP);
                        }

                        if (quickBackgroundCheck || invisibleStreak[instanceKey] >= THRESHOLD_MINIMIZE) {
                            nextState = true;
                        }
                    } else {
                        visibleStreak[instanceKey]++;
                        invisibleStreak[instanceKey] = 0;
                        
                        // Icon will only un-dim if the app threads stay awake continuously 
                        // for 33 frames, filtering out temporary spikes completely.
                        if (visibleStreak[instanceKey] >= THRESHOLD_MAXIMIZE) {
                            nextState = false;
                        }
                    }
                }

                // Smoothly toggle the state for non-Tracker applications if outside grace period
                if (!inGracePeriod) {
                    activeTaskWin.isMinimized = nextState;          
                }
        
            }             
                
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
		
		    // B. Draw an active native glowing mini ledger stripe indicator line below active windows
		    glDisable(GL_TEXTURE_2D);
		    if (activeTaskWin.isMinimized == false && activeTaskWin.title != "Tracker") {
		        // Bright glowing electric blue highlight segment line for focused open panels
		        glColor4f(0.2f, 0.6f, 1.0f, 0.9f); 
		        glLineWidth(3.0f);
		        glBegin(GL_LINES);
		            glVertex2f(iconBounds.left + 4.0f, dockPlate.bottom - 4.0f);
		            glVertex2f(iconBounds.right - 4.0f, dockPlate.bottom - 4.0f);
		        glEnd();
		    }
		
				    currentX += size + padding;
		    renderingSlotIdx++;
		}
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Reset texture filters cleanly

       // Left Clock Divider Line
	       float lineLeftSnappedX = std::floor(currentX + 0.5f);
	       glLineWidth(2.0f); glColor4f(0.15f, 0.15f, 0.15f, 0.5f);
	       glBegin(GL_LINES);
	           glVertex2f(lineLeftSnappedX, dockPlate.top + 8.0f);
	           glVertex2f(lineLeftSnappedX, dockPlate.bottom - 8.0f);
	       glEnd();
	       
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
		/*
        if (fTrashRect.Contains(fMouseX, fMouseY)) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            if (!fTrashTextGenerated) {
                if (fTrashTooltipTexId != 0) { glDeleteTextures(1, &fTrashTooltipTexId); fTrashTooltipTexId = 0; }
                auto generatedTex = RenderTextToTexture("Middle click to empty Trash", &fTrashTooltipW, &fTrashTooltipH);
                fTrashTooltipTexId = generatedTex.id; 
                fTrashTextGenerated = true; 
            }

            float tooltipW = static_cast<float>(fTrashTooltipW) + 12.0f;
            float tooltipH = static_cast<float>(fTrashTooltipH) + 8.0f;
            float tooltipLeft = fTrashRect.left + ((fTrashRect.right - fTrashRect.left) / 2.0f) - (tooltipW / 2.0f);
            float tooltipBottom = fTrashRect.top - 1.0f; 

            HaikuRect tooltipBounds = { tooltipLeft, tooltipBottom - tooltipH, tooltipLeft + tooltipW, tooltipBottom };
            DrawFilledRect(tooltipBounds, 0.15f, 0.15f, 0.15f, 0.75f);

            glColor4f(0.10f, 0.10f, 0.10f, 0.5f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(tooltipBounds.left,  tooltipBounds.top);   glVertex2f(tooltipBounds.right, tooltipBounds.top);
                glVertex2f(tooltipBounds.right, tooltipBounds.bottom); glVertex2f(tooltipBounds.left,  tooltipBounds.bottom);
            glEnd();
			
            if (fTrashTooltipTexId != 0) {

                glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fTrashTooltipTexId);
                
                // =========================================================================
                // FORCE NEON GREEN COLOR BY OVERRIDING BLACK TEXTURE RGB CHANNELS
                // =========================================================================
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_PRIMARY_COLOR);
                
                // Set the blending color filter to match your illuminated matrix neon green
                glColor4f(0.2f, 1.0f, 0.2f, 1.0f); 
                // =========================================================================
                
                float textX = tooltipBounds.left + 6.0f; float textY = tooltipBounds.top + 4.0f;
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textX, textY);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textX + fTrashTooltipW, textY);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textX + fTrashTooltipW, textY + fTrashTooltipH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textX, textY + fTrashTooltipH);
                glEnd();
                
                // =========================================================================
                // Cleanly reset texture environment mode back to standard modulation
                // =========================================================================
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                // =========================================================================
            }
			
        } else {
            fTrashTextGenerated = false;
        }
		*/
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
	
	        // --- CRITICAL DEFENSIVE SHIELD: FORCE FULL OpenGL STATE SHUTDOWN ---
	        // This explicitly cuts off the texture matrix pipeline, guaranteeing the clock text 
	        // and trash bin drawing routines downstream inherit a pristine state machine!
	        glBindTexture(GL_TEXTURE_2D, 0);
	        glDisable(GL_TEXTURE_2D);
	        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	        // =========================================================================
	
	        currentX += dynamicTrayWidth;
        }





        // =========================================================================
        // 6. DRAW SYSTEM CLOCK STATUS TEXT (MATHEMATICALLY LOCKED SYMMETRY)
        // =========================================================================
        if (fClockTexture.id != 0) {

            float clockScale   = dynamicScales[clockSlotIdx];
            float dynamicClockW = dynamicWidths[clockSlotIdx]; 
            float highDpiCompensateFactor = 0.42f; 
            float dynamicClockH = static_cast<float>(fClockHeight) * highDpiCompensateFactor * clockScale;

            currentX += clockSectionPadding;
            float clockY = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicClockH / 2.0f));
            
            HaikuRect clockB = { 
                std::floor(currentX + 0.5f), 
                std::floor(clockY + 0.5f), 
                std::floor(currentX + dynamicClockW + 0.5f), 
                std::floor(clockY + dynamicClockH + 0.5f) 
            };
            
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fClockTexture.id);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f); 
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(clockB.left, clockB.top);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(clockB.right, clockB.top);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(clockB.right, clockB.bottom);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(clockB.left, clockB.bottom);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
            
            currentX += dynamicClockW;
        }

        // =========================================================================
        // NEW: DRAW DYNAMIC VOLUME CONTROL SLIDER (RIGHT OF THE CLOCK)
        // =========================================================================
        FetchHaikuMixerVolume(); 
        
        currentX += clockSectionPadding;
        
        float volScale          = dynamicScales[volumeSlotIdx];
        float dynamicVolWidth   = dynamicWidths[volumeSlotIdx];
        float dynamicVolHeight  = 12.0f * volScale; 
        float volTop = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicVolHeight / 2.0f));
        
        // FIX: Cache these exact screen measurements into class memory fields 
        // every single frame so HandleMouseInput can access them safely!
        fCachedVolLeft   = currentX;
        fCachedVolTop    = volTop;
        fCachedVolWidth  = dynamicVolWidth;
        fCachedVolHeight = dynamicVolHeight;

        HaikuRect volBounds = { currentX, volTop, currentX + dynamicVolWidth, volTop + dynamicVolHeight };

        // 1. Draw the slider background dark casing trough
        DrawFilledRect(volBounds, 0.05f, 0.10f, 0.05f, 0.9f); 
        
        // 2. Draw active volume fill level
        HaikuRect activeVolumeFill = {
            volBounds.left,
            volBounds.top,
            volBounds.left + (dynamicVolWidth * fCurrentVolumeLevel),
            volBounds.bottom
        };
        DrawFilledRect(activeVolumeFill, 0.2f, 1.0f, 0.2f, 0.85f);

        // 3. Draw a thin perimeter frame outline edge loop around the slider case boundary
        glColor4f(0.15f, 0.15f, 0.15f, 0.6f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(volBounds.left,  volBounds.top);    glVertex2f(volBounds.right, volBounds.top);
            glVertex2f(volBounds.right, volBounds.bottom); glVertex2f(volBounds.left,  volBounds.bottom);
        glEnd();

        currentX += dynamicVolWidth;




        // =========================================================================
        // 6B. DRAW GRAPHICAL PURPLE BOUNCING CPU METERS (ROUNDED CORNER CASING)
        // =========================================================================

        glLineWidth(2.0f);        
        currentX += clockSectionPadding;        
        float cpuScale          = dynamicScales[cpuSlotIdx];
        float dynamicGraphWidth = dynamicWidths[cpuSlotIdx];
        float dynamicGraphHeight = 28.0f * cpuScale; 
        float graphTop = dockPlate.bottom - 10.0f - ((maxDockHeight / 2.0f) + (dynamicGraphHeight / 2.0f));
        
        HaikuRect cpuGraphBounds = { currentX, graphTop, currentX + dynamicGraphWidth, graphTop + dynamicGraphHeight };

        // 1. FIXED: Draw solid dark background container equipped with a clean 4.0-pixel rounding radius
        DrawGLRoundedRect(cpuGraphBounds, 4.0f, 0.05f, 0.05f, 0.08f, 0.9f, true); 
        
        UpdateGlobalCpuLoadTracker();

        int numBars = (fCpuHistoryIndex > 0 && fCpuHistoryIndex <= 40) ? fCpuHistoryIndex : 16;
        float barSpacing = 1.5f;
        float totalSpacingSpace = barSpacing * (numBars + 1);
        float barWidth = (dynamicGraphWidth - totalSpacingSpace) / numBars;

        static std::vector<float> visualBouncingHeights(40, 0.0f);

        glBegin(GL_QUADS);
        for (int i = 0; i < numBars; ++i) {
            float targetLoadFactor = fCpuHistory[i];
            visualBouncingHeights[i] = (visualBouncingHeights[i] * 0.82f) + (targetLoadFactor * 0.18f);

            float barLeft = cpuGraphBounds.left + barSpacing + (i * (barWidth + barSpacing));
            float barRight = barLeft + barWidth;
            
            // Constrain the bar top math to match our internal container corner curve parameters safely
            float barTop = cpuGraphBounds.bottom - (visualBouncingHeights[i] * (dynamicGraphHeight - 2.0f)) - 1.0f;
            
            glColor4f(0.57f, 0.12f, 0.99f, 0.90f); // Neon Purple
            
            glVertex2f(barLeft,  barTop);
            glVertex2f(barRight, barTop);
            glVertex2f(barRight, cpuGraphBounds.bottom - 1.0f);
            glVertex2f(barLeft,  cpuGraphBounds.bottom - 1.0f);
        }
        glEnd();

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




void LoadConfiguration() {
    BPath path;
    gFavoritePaths.clear(); // Reset to prevent duplicate tracking anomalies
    
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("hdesktop_settings");
        
        BFile file(path.Path(), B_READ_ONLY);
        BMessage settings;
        
        if (settings.Unflatten(&file) == B_OK) {
            // 1. Recover existing settings
            bool savedValue;
            if (settings.FindBool("auto_hide", &savedValue) == B_OK) {
                autoHideEnabled = savedValue;
            }
            if (settings.FindBool("sys_tray", &savedValue) == B_OK) {
                showSystemTray = savedValue;
            }
            if (settings.FindBool("auto_raise", &savedValue) == B_OK) {
                dockAlwaysOnTop = savedValue;
            }
            
            // 2. Recover the favorites string index array
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
}

void SaveConfiguration() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("hdesktop_settings");
        
        BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        BMessage settings;
        
        // 1. Pack existing settings
        settings.AddBool("auto_hide",  autoHideEnabled);
        settings.AddBool("sys_tray",   showSystemTray);
        settings.AddBool("auto_raise", dockAlwaysOnTop);
        
        // 2. Pack all live favorites keys sequentially into the same field name
        std::set<std::string>::iterator it;
        for (it = gFavoritePaths.begin(); it != gFavoritePaths.end(); ++it) {
            settings.AddString("favorite_apps", it->c_str());
        }
        
        settings.Flatten(&file); 
    }
}

// =========================================================================
// ASYNC CPU MENU RUNNER 
// =========================================================================
void AsyncCpuMenuRunner::_DisplayCPUGraphMenu() {
    std::cout << "[CPU Graph] Initializing dynamic async system activity menu..." << std::endl;

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
    
    float anchoredMenuY = static_cast<float>(fArgs->winY) - 5.0f;
    BPoint screenClickPoint(anchoredMenuX, anchoredMenuY);

    // Notice we use Go(..., false, false) intentionally here!
    // Since this method runs exclusively within our separate worker looper window thread, 
    // blocking synchronously here is completely safe and won't lock your main SDL loop.
    BMenuItem* chosenAction = pcMenu->Go(screenClickPoint, false, false);

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
                            std::cout << "[CPU Graph] Forcing termination of application process: " 
                                      << thName << " (Team ID " << targetTeam << ")" << std::endl;
                            kill_team(targetTeam); 
                        } else {
                            std::cout << "[CPU Graph] Force termination cancelled by user." << std::endl;
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
                            std::cout << "[CPU Graph] Terminating application cleanly: " << appName << " (Team " << targetTeam << ")" << std::endl;
                            
                            BMessenger appTarget(nullptr, targetTeam);
                            if (appTarget.IsValid()) {
                                appTarget.SendMessage(B_QUIT_REQUESTED);
                            } else {
                                kill_team(targetTeam); 
                            }
                        } else {
                            std::cout << "[CPU Graph] Clean termination cancelled by user." << std::endl;
                        }
                    }
                    break;
                }


                case 'pwrS': {
                    std::cout << "[CPU Graph] Activating system Power Saving configuration profiles..." << std::endl;
                    std::system("/boot/system/apps/PowerStatus --toggle &"); 
                    break;
                }

                default:
                    std::cout << "[CPU Graph] Unhandled menu flag: " << actionMsg->what << std::endl;
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
    glViewport(0, 0, screenWidth, 140);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // --- WALLPAPER RE-STITCH ALIGNMENT MATH ---
    float panelTopY    = static_cast<float>(screenHeight) - 140.0f;
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
    const char* localVersion = "v1.0.19"; 
    char updateCmd[1024];
    snprintf(updateCmd, sizeof(updateCmd),
        "(REMOTE_V=$(curl -sL \"%s\" | tr -d '\\r\\n'); "
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
                    std::cout << "[Node Monitor] Wallpaper update detected! Refreshing dock canvas asset." << std::endl;
                    
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
                
                    int hiddenScreenOffset = screenHeight - 140; 
                    int adjustedMouseY = mouseY + hiddenScreenOffset;
                
                    // Feed smooth radial zoom parameters
                    desktopEngine.HandleMouseInput(mouseX, adjustedMouseY, buttons);
                
                    if (incomingEventPackage.type == SDL_MOUSEBUTTONDOWN) {
                        if (incomingEventPackage.button.button == SDL_BUTTON_LEFT || 
                            incomingEventPackage.button.button == SDL_BUTTON_RIGHT ||
                            incomingEventPackage.button.button == SDL_BUTTON_MIDDLE) {

                            desktopEngine.HandleMouseClick(mouseX, adjustedMouseY, incomingEventPackage.button.button);
 				            if (be_app && be_app->Lock()) {                            	
				                int32 windowCount = be_app->CountWindows();
				                if (windowCount > 0) {
				                    BWindow* win = be_app->WindowAt(0);
				                    if (win != nullptr && win->Lock()) {
				                        win->SendBehind(nullptr);
				                        win->Unlock();
				                    }
				                }
				                be_app->Unlock();
				                
				            }
				            
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

        // 2. DEFINE PHANTOM PADDING BUFFERS (Adjust these to tune the friction feel!)
        int horizontalPadding = 300; // Extra width padding to prevent horizontal snapping
        int verticalTopPadding = 2; // Extra overhead clearance padding for diagonal exits

        // Calculate padded left and right limits
        int paddedLeftX  = (dockPanelW / 2) - (static_cast<int>(currentDynamicWidth) / 2) - horizontalPadding;
        int paddedRightX = (dockPanelW / 2) + (static_cast<int>(currentDynamicWidth) / 2) + horizontalPadding;

        // 3. EVALUATE HVER COMPLIANCE USING PADDED VALUES
        if (dockState == STATE_HIDDEN) {
            cursorIsInsideDock = (localMouseX >= paddedLeftX && localMouseX <= paddedRightX &&
                                  localMouseY >= (dockPanelH - sensorHeight) && localMouseY < dockPanelH);
        } else {
            // Include verticalTopPadding to extend the tracking area above the dock shelf
            cursorIsInsideDock = (localMouseX >= paddedLeftX && localMouseX <= paddedRightX &&
                                  localMouseY >= -verticalTopPadding && localMouseY < dockPanelH);
        }

        // Pass this padded layout verification flag directly into your engine instance
        desktopEngine.fCursorIsInsideHitbox = cursorIsInsideDock;

        // =========================================================================
        // EDGE-TRIGGERED NATIVE HOVER LAYERING SYSTEM (STRICT TRACKER DISCRIMINATOR)
        // =========================================================================
        static bool lastHoverState = false; 
        
        if (dockAlwaysOnTop && (cursorIsInsideDock != lastHoverState)) {
            lastHoverState = cursorIsInsideDock; 
            
            if (be_app && be_app->Lock()) {
                int32 windowCount = be_app->CountWindows();
                for (int32 i = 0; i < windowCount; i++) {
                    BWindow* win = be_app->WindowAt(i);
                    if (win != nullptr && win->Lock()) {
                        uint32 flags = win->Flags();

                        if (cursorIsInsideDock) {
                            // 1. Elevate instantly above all standard applications
                            win->SetFeel(B_FLOATING_ALL_WINDOW_FEEL);
                            flags &= ~B_AVOID_FRONT;
                            flags &= ~B_AVOID_FOCUS;
                            
                            win->Activate(true);
                        } else {
                            // 2. Return to normal window layer
                            win->SetFeel(B_NORMAL_WINDOW_FEEL);
                            flags |= B_AVOID_FRONT;
                            flags |= B_AVOID_FOCUS;
                            
                            // 3. STRICT SYSTEM TARGET CHECKERS
                            bool activeAppIsTracker = false;
                            app_info activeAppInfo;
                            
                            // Query the roster right at the moment of exit boundary trip
                            if (be_roster && be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
                                if (strcmp(activeAppInfo.signature, "application/x-vnd.Be-TRAK") == 0) {
                                    activeAppIsTracker = true;
                                }
                            }

                            if (activeAppIsTracker) {
                                // STRICT FOR TRACKER: Execute your original working code path perfectly.
                                // This forces the focus back down to Tracker windows and preserves minimize/restore.
                                win->SendBehind(nullptr);
                            } else {
                                // FIX FOR ALL OTHER APPS: Force the OS to instantly restore the full active 
                                // highlighted window border look to the application currently beneath the mouse!
                                if (be_roster && be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
                                    be_roster->ActivateApp(activeAppInfo.team);
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
            needsRender = true;
        }

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

        int hiddenScreenOffset = screenHeight - 140;
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
            // A. Instruct your engine to paint the core dock backdrop textures
            desktopEngine.RenderFrame(currentY);   
            
            // B. Flush out the double buffer canvas matrix to the monitor screen display safely
            SDL_GL_SwapWindow(window);
            needsRender = false; 
        }

    }

    std::cout << "[System Terminal] Closing hDesktop context cleanly." << std::endl;

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
