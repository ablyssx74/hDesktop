/*
 * Copyright 2026, Kris Beazley hDesktop@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_syswm.h>
#include <GL/glu.h>
#include <Bitmap.h>
#include <IconUtils.h>
#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <NodeInfo.h>
#include <Node.h>
#include <iostream>
#include <vector>
#include <string>
#include <View.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <cmath>
#include <Roster.h>
#include <ctime>
#include <TranslationUtils.h>
#include <OS.h>
#include <AppKit.h>
#include <StorageKit.h>
#include <vector>
#include <AppServerLink.h> 
#include <cstring>
#include <Window.h>
#include <FindDirectory.h>
#include <String.h>
#include <fs_attr.h>
#include <Message.h>
#include <stdio.h>
#include <cstdio>
#include <cstdlib> 
#include <ScrollView.h>
#include <Screen.h>
#include <map>





class HaikuAppDrawerWindow; 
HaikuAppDrawerWindow* gActiveDrawerInstance = nullptr; 

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
            delete item->icon;
            delete item;
        }
    }
    
   virtual void MouseMoved(BPoint point, uint32 transit, const BMessage* message) {
        // Force the app cell grid canvas to instantly refresh as your cursor glides across choices
        Invalidate(); 
    }


    void ScanSystemDirectories() {
        const char* paths[] = { "/boot/system/apps", "/boot/system/preferences" };
        for (int p = 0; p < 2; p++) {
            BDirectory dir(paths[p]);
            if (dir.InitCheck() != B_OK) continue;

            BEntry entry;
            while (dir.GetNextEntry(&entry) == B_OK) {
                char name[B_FILE_NAME_LENGTH];
                if (entry.GetName(name) != B_OK) continue;

                DrawerItem* item = new DrawerItem();
                item->name = name;
                entry.GetRef(&item->ref);
                
                // Allocate native 32x32 color matrix profile
                item->icon = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
                
                bool iconLoaded = false;
                
                // --- CONDITION A: ENTRY IS A SUB-DIRECTORY/FOLDER ---
                if (entry.IsDirectory()) {
                    // Fetch the standard Haiku platform folder icon via the system MIME database
                    BMimeType folderMime("application/x-vnd.Be-directory");
                    if (folderMime.InitCheck() == B_OK) {
                        if (folderMime.GetIcon(item->icon, B_LARGE_ICON) == B_OK) {
                            iconLoaded = true;
                        }
                    }
                }
                // --- CONDITION B: ENTRY IS A REGULAR FILE/BINARY ---
                else {
                    BNodeInfo nodeInfo;
                    BNode node(&entry);
                    if (nodeInfo.SetTo(&node) == B_OK) {
                        if (nodeInfo.GetIcon(item->icon, B_LARGE_ICON) == B_OK) {
                            iconLoaded = true;
                        }
                    }
                }

                // If both native lookups failed to fetch an asset, apply a safe global generic fallback
                if (!iconLoaded) {
                    BMimeType genericMime("application/octet-stream");
                    if (genericMime.InitCheck() != B_OK || genericMime.GetIcon(item->icon, B_LARGE_ICON) != B_OK) {
                        // If everything fails, clean up the pointer safely to avoid ghost boxes
                        delete item->icon;
                        item->icon = nullptr;
                    }
                }
                
                fItemsList.AddItem(item);
            }
        }
    }


    virtual void Draw(BRect updateRect) {
        float itemW = 100.0f;
        float itemH = 110.0f;
        float startX = 30.0f;
        float startY = 30.0f;
        float spacingX = 24.0f;
        float spacingY = 20.0f;

        int32 cols = static_cast<int32>((Bounds().Width() - (startX * 2.0f)) / (itemW + spacingX));
        if (cols < 1) cols = 1;

        // Fetch active cursor coordinates relative to our scroll canvas context area
        BPoint cursorPoint;
        uint32 transitButtons;
        GetMouse(&cursorPoint, &transitButtons, false); // Real-time mouse position track

        // =========================================================================
        // BACKLIGHT HOVER HIGHLIGHT FILTER PASS
        // =========================================================================
        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            int32 c = i % cols;
            int32 r = i / cols;

            float x = startX + (c * (itemW + spacingX));
            float y = startY + (r * (itemH + spacingY));

            BRect itemBounds(x, y, x + itemW, y + itemH);
            
            // If the cursor glides inside this specific application block, draw the glow card!
            if (itemBounds.Contains(cursorPoint)) {
                SetDrawingMode(B_OP_ALPHA);
                
                // Draw soft translucent glowing backlight rectangle behind the shortcut target
                // Light slate overlay with ~12% opacity matches premium interfaces beautifully
                SetHighColor(rgb_color{100, 110, 140, 30}); 
                FillRect(itemBounds);
                
                // Add crisp outline tracing borders around the hovered choice card
                SetHighColor(rgb_color{130, 145, 180, 70});
                StrokeRect(itemBounds);
            }
        }

        // =========================================================================
        // INDIVIDUAL ICON AND STRING LABEL RENDERING PASS (YOUR ORIGINAL RENDERING CODE)
        // =========================================================================
        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            DrawerItem* item = (DrawerItem*)fItemsList.ItemAt(i);
            int32 c = i % cols;
            int32 r = i / cols;

            float x = startX + (c * (itemW + spacingX));
            float y = startY + (r * (itemH + spacingY));

            if (item->icon) {
                SetDrawingMode(B_OP_ALPHA);
                DrawBitmap(item->icon, BPoint(x + (itemW / 2.0f) - 16.0f, y + 15.0f));
            }

            SetHighColor(rgb_color{240, 240, 245, 255});
            BString truncatedName = item->name;
            TruncateString(&truncatedName, B_TRUNCATE_END, itemW - 10.0f);
            float textW = StringWidth(truncatedName.String());
            DrawString(truncatedName.String(), BPoint(x + (itemW / 2.0f) - (textW / 2.0f), y + 80.0f));
        }


        // Dynamically update inner container tracking height ceilings so scrolling limits scale cleanly
        int32 colsRecalc = static_cast<int32>((Bounds().Width() - (startX * 2.0f)) / (itemW + spacingX));
        if (colsRecalc < 1) colsRecalc = 1;
        int32 totalRows = (fItemsList.CountItems() + colsRecalc - 1) / colsRecalc;
        float targetVirtualHeight = (totalRows * (itemH + spacingY)) + startY + 40.0f;
        
        if (Bounds().Height() != targetVirtualHeight) {
            ResizeTo(Bounds().Width(), targetVirtualHeight);
        }
    }

    virtual void MouseDown(BPoint point) {
        float itemW = 100.0f;
        float itemH = 110.0f;
        float startX = 30.0f;
        float startY = 30.0f;
        float spacingX = 24.0f;
        float spacingY = 20.0f;

        int32 cols = static_cast<int32>((Bounds().Width() - (startX * 2.0f)) / (itemW + spacingX));
        if (cols < 1) cols = 1;

        for (int32 i = 0; i < fItemsList.CountItems(); i++) {
            DrawerItem* item = (DrawerItem*)fItemsList.ItemAt(i);
            int32 c = i % cols;
            int32 r = i / cols;

            float x = startX + (c * (itemW + spacingX));
            float y = startY + (r * (itemH + spacingY));

            BRect itemBounds(x, y, x + itemW, y + itemH);
            if (itemBounds.Contains(point)) {
                std::cout << "[Native Drawer] Selected Item Hook: " << item->name.String() << std::endl;
                be_roster->Launch(&item->ref);
                
                // FIXED CLOSE OVERRIDE: Destroys the drawer window instance instantly on launcher execution click
                if (Window()) {
                    Window()->Quit(); 
                }
                return;
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


// =========================================================================
// MODERN OPENGL FRAMEBUFFER EXTENSION PROTOTYPES FOR HAIKU OS
// =========================================================================
typedef void (APIENTRY * PFNGLGENFRAMEBUFFERSPROC) (GLsizei n, GLuint *framebuffers);
typedef void (APIENTRY * PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef void (APIENTRY * PFNGLFRAMEBUFFERTEXTURE2DPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRY * PFNGLCHECKFRAMEBUFFERSTATUSPROC) (GLenum target);

// Global function pointers
PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC      glFramebufferTexture2D = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus = nullptr;


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
    HaikuTexture icon;       
    bool isMinimized;       
    bool* openStateFlag;     
    bool* minimizeStateFlag; 
    team_id teamId;       
};



// 2. Complex Structural Groupings Dependent on Base Types
struct DesktopIconItem {
    std::string name;
    HaikuTexture texture;       // Core 48x48 icon file asset
    HaikuTexture textTexture;   // Dynamic text string label texture
    HaikuRect bounds;
    HaikuRect textBounds;       // Layout boundaries for label text box below the icon
    bool isFolder;
};



enum {
    BG_MODE_CENTER  = 1,
    BG_MODE_TILE    = 2,
    BG_MODE_STRETCH = 3,
    BG_MODE_SCALE   = 4
};

status_t ForceActiveWallpaperMode(int32 newMode) {
    BPath desktopPath;
    status_t status = find_directory(B_DESKTOP_DIRECTORY, &desktopPath);
    if (status != B_OK) return status;

    BNode desktopNode(desktopPath.Path());
    status = desktopNode.InitCheck();
    if (status != B_OK) return status;

    attr_info info;
    status = desktopNode.GetAttrInfo("be:bgndimginfo", &info);
    if (status != B_OK || info.size <= 0) return status;

    char* buffer = new(std::nothrow) char[info.size];
    if (buffer == nullptr) return B_NO_MEMORY;

    // 1. Read the current configuration message
    if (desktopNode.ReadAttr("be:bgndimginfo", info.type, 0, buffer, info.size) != info.size) {
        delete[] buffer;
        return B_IO_ERROR;
    }

    BMessage container;
    status = container.Unflatten(buffer);
    delete[] buffer; 
    if (status != B_OK) return status;

    // 2. Identify the active workspace mask to target the correct array item
    int32 currentWorkspaceIndex = current_workspace();
    uint32 currentWorkspaceMask = 1 << currentWorkspaceIndex;
    bool matchedAndUpdated = false;

    // 3. Loop through indices to locate our active workspace entry
    for (int32 index = 0; ; index++) {
        int32 workspaceMask = 0;
        if (container.FindInt32("be:bgndimginfoworkspaces", index, &workspaceMask) != B_OK) {
            break; 
        }

        if ((workspaceMask & currentWorkspaceMask) != 0) {
            status = container.ReplaceInt32("be:bgndimginfomode", index, newMode);
            if (status == B_OK) {
                matchedAndUpdated = true;
            }
            break; 
        }
    }

    // Fallback to index 0 if workspace mapping wasn't matched explicitly
    if (!matchedAndUpdated) {
        status = container.ReplaceInt32("be:bgndimginfomode", 0, newMode);
        if (status != B_OK) return status;
    }

    // 4. Flatten the updated BMessage back into raw bytes
    BMallocIO mallocIO;
    status = container.Flatten(&mallocIO);
    if (status != B_OK) return status;

    // 5. Rewrite the data back into Tracker's persistent node attribute
    ssize_t bytesWritten = desktopNode.WriteAttr(
        "be:bgndimginfo", 
        info.type, 
        0, 
        mallocIO.Buffer(), 
        mallocIO.BufferLength()
    );

    if (bytesWritten != (ssize_t)mallocIO.BufferLength()) {
        return B_IO_ERROR;
    }

    // 6. Force Tracker to refresh by executing the native Haiku background refresher command
    std::system("hey Tracker 'wbre' to Desktop");

    return B_OK;
}



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


    void HandleMouseWheel(int wheelStepY) {
        // Define velocity multiplication constant (30 pixels skipped per mouse notch tick)
        float scrollSpeed = 30.0f;
        
        // SDL wheels supply a value of 1 for scrolling up, and -1 for down
        fScrollOffset -= static_cast<float>(wheelStepY) * scrollSpeed;
        
        // Clamp bounds limits so you can't scroll past the list ends
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
	            bool isDuplicate = false;
	            for (const auto& sig : processedSignatures) {
	                if (sig == appSignature && !appSignature.empty()) {
	                    isDuplicate = true; break;
	                }
	            }
	            if (isDuplicate) continue; 
	
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
	
	            processedSignatures.push_back(appSignature);
	            fTaskbarWindows.push_back(openApp);
	        }
	    }
	}


   
	void HandleMouseClick(int x, int y, int button) {
	    // Sync global mouse variables to match click coordinates
	    fMouseX = x; 
	    fMouseY = y;

	
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
	
	    std::vector<float> dynamicWidths;
	    std::vector<float> dynamicScales;
	    float totalDockWidth = 0.0f;
	    float maxDockHeight = baseSize;
	
	    // Step 1: Calculate the exact dynamic width of the primary launcher panel zone
	    for (size_t i = 0; i < totalIconsCount; ++i) {
	        float approximateIconX = (fWidth / 2.0f) - ((totalIconsCount * (baseSize + padding)) / 2.0f) + (i * (baseSize + padding)) + (baseSize / 2.0f);
	        float distanceX = std::abs(x - approximateIconX); // Evaluated via local click 'x'
	        
	        float scale = 1.0f;
	        if (y >= (fHeight - 140.0f)) { // Evaluated via local click 'y'
	            if (distanceX < 160.0f) {
	                float ratio = distanceX / 160.0f;
	                scale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	            }
	        }
	
	        float finalSize = baseSize * scale;
	        dynamicWidths.push_back(finalSize);
	        dynamicScales.push_back(scale);
	        totalDockWidth += finalSize + padding;
	        if (finalSize > maxDockHeight) maxDockHeight = finalSize;
	    }
	    totalDockWidth -= padding;
	
	    float clockSectionPadding = 24.0f;
	    float cpuGraphWidth = 60.0f;
	    float separatorGapPadding = 16.0f;
	
	    // Step 2: Simulate Section 3's Unified Predictive Geometry Loop
	    float baseTrashSize = 48.0f;
	    float trashScale = 1.0f;
	    float currentPredictX = 20.0f + totalDockWidth;
	    if (activeWindowsCount > 0) {
	        currentPredictX += separatorGapPadding;
	    }
	    
	    float traySectionStartX = currentPredictX;
	
	    if (fClockTexture.id != 0) {
	        currentPredictX += clockSectionPadding + fClockWidth;
	    }
	
	    currentPredictX += clockSectionPadding;
	    float approximateTrashX = (fWidth / 2.0f) - ((totalDockWidth + (currentPredictX + (baseTrashSize / 2.0f) - traySectionStartX)) / 2.0f) + currentPredictX + (baseTrashSize / 2.0f);
	    float distanceTrashX = std::abs(x - approximateTrashX);
	
	    if (y >= (fHeight - 140.0f)) {
	        if (distanceTrashX < 160.0f) {
	            float ratio = distanceTrashX / 160.0f;
	            trashScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
	        }
	    }
	    float dynamicTrashSize = baseTrashSize * trashScale;
	    if (dynamicTrashSize > maxDockHeight) maxDockHeight = dynamicTrashSize;
	    currentPredictX += dynamicTrashSize + clockSectionPadding + cpuGraphWidth;
	
	    float finalTrayWidth = currentPredictX - traySectionStartX;
	    float finalPlateWidth = totalDockWidth + finalTrayWidth + clockSectionPadding;
	    if (activeWindowsCount > 0) {
	        finalPlateWidth += separatorGapPadding;
	    }
	
	    float dockMarginBottom = 15.0f;
	    HaikuRect dockPlate;
	    dockPlate.left = (fWidth / 2.0f) - (finalPlateWidth / 2.0f) - 20.0f;
	    dockPlate.right = (fWidth / 2.0f) + (finalPlateWidth / 2.0f) + 20.0f;
	    dockPlate.bottom = fHeight - dockMarginBottom;
	    dockPlate.top = dockPlate.bottom - maxDockHeight - 20.0f;
	

	
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
	
	        if (x >= realIconBounds.left && x <= realIconBounds.right &&
	            y >= realIconBounds.top  && y <= realIconBounds.bottom) {
	            
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
	
	            // --- TRACKER SAFETY TOGGLE PIPELINES ---
	            bool isTracker = false;
	            app_info appInfo;
	            if (be_roster->GetRunningAppInfo(activeTaskWin.teamId, &appInfo) == B_OK) {
	                if (strcmp(appInfo.signature, "application/x-vnd.Be-TRAK") == 0) isTracker = true;
	            }
	
	            if (isTracker) {
	                char safeTrackerCmdBuffer[256];
	                BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	
	                if (activeTaskWin.isMinimized == false) {
	                    // ACTION A: Tracker windows are currently visible -> HIDE THEM
	                    std::cout << "[SYSTEM FIX] ---> Action: NATIVELY HIDING TRACKER FOLDERS" << std::endl;
	                    
	                    // Minimize Window 1 and up to ensure the Desktop sheet (Window 0) stays completely active
	                    std::snprintf(safeTrackerCmdBuffer, sizeof(safeTrackerCmdBuffer),
	                        "hey \"Tracker\" Set Minimize of Window 1 to true &");
	                    std::system(safeTrackerCmdBuffer);
	                    
	                    be_roster->ActivateApp(-1);
	                    activeTaskWin.isMinimized = true;
	                } 
	                else {
	                    // ACTION B: Tracker is minimized or completely empty -> WAKE / SPAWN IT
	                    std::cout << "[SYSTEM FIX] ---> Action: ENSURING TRACKER FOLDER IS SPUN UP" << std::endl;
	                    
	                    // 1. Bring Tracker application team to the foreground
	                    be_roster->ActivateApp(activeTaskWin.teamId);
	                    
	                    // 2. Unminimize any folder views that might already be sleeping at index 1
	                    std::snprintf(safeTrackerCmdBuffer, sizeof(safeTrackerCmdBuffer),
	                        "hey \"Tracker\" Set Minimize of Window 1 to false &");
	                    std::system(safeTrackerCmdBuffer);
	
	                    // 3. Always broadcast a native open request. If a folder was already open, Tracker 
	                    // safely ignores this. If Tracker had 0 windows open, this instantly spawns /boot/home.
	                    if (trackerMessenger.IsValid()) {
	                        BEntry homeEntry("/boot/home");
	                        entry_ref homeRef;
	                        if (homeEntry.GetRef(&homeRef) == B_OK) {
	                            BMessage openMsg(B_REFS_RECEIVED);
	                            openMsg.AddRef("refs", &homeRef);
	                            trackerMessenger.SendMessage(&openMsg);
	                        }
	                    }
	                    
	                    activeTaskWin.isMinimized = false;
	                }
	
	                std::cout << "========================================\n" << std::endl;
	                return; // ABSOLUTE SHIELD: Double-fire mouse loops are terminated safely here
	            }
	            // -----------------------------------------------------------
	
	            // GENERAL APPLICATION BEHAVIOR (NON-TRACKER APPS)
	            char systemCmdBuffer[512]; 
	
	            if (activeTaskWin.isMinimized == false) {
	                std::cout << "[SYSTEM CALL FIX] ---> Action: RUNNING 'hey' TO MINIMIZE WINDOW SUITE" << std::endl;
	                
	                std::snprintf(systemCmdBuffer, sizeof(systemCmdBuffer),
	                    "hey \"%s\" Set Minimize of Window 0 to true &", 
	                    activeTaskWin.title.c_str());
	                
	                std::system(systemCmdBuffer); 
	                be_roster->ActivateApp(-1);
	                activeTaskWin.isMinimized = true; 
	            } 
	            else {
	                std::cout << "[SYSTEM CALL FIX] ---> Action: RESTORING VIA ROSTER & 'hey' UNMINIMIZE" << std::endl;
	                
	                be_roster->ActivateApp(activeTaskWin.teamId);
	                
	                std::snprintf(systemCmdBuffer, sizeof(systemCmdBuffer),
	                    "hey \"%s\" Set Minimize of Window 0 to false &", 
	                    activeTaskWin.title.c_str());
	                
	                std::system(systemCmdBuffer);
	                activeTaskWin.isMinimized = false; 
	            }
	            
	            std::cout << "========================================\n" << std::endl;
	            return; 
	        }
	        
	        currentX += size + padding;
	        evaluationSlotIdx++;
	    }
	
	    // =========================================================================
	    // STEP C: EVALUATE SYSTEM TRAY COMPONENTS (CLOCK -> TRASH BIN -> CPU)
	    // =========================================================================
	    if (x >= fTrashRect.left && x <= fTrashRect.right &&
	        y >= fTrashRect.top  && y <= fTrashRect.bottom) {
	        
	        // Clear active overlay drawer heights if jumping to trash operations
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
	        
	        else if (button == SDL_BUTTON_RIGHT) {
	            std::system("trash --empty &"); 
	            fLastTrashCheckTime = 0; 
	            return;
	        }
	    }
	}



                	

    void HandleMouseInput(int x, int y, Uint32 buttonState) {    	
        fMouseX = x; fMouseY = y;
        fIsResizing = false;
       if (fShowMainMenu && !fMainMenuBounds.Contains(x, y)) {
           if (y < fHeight - 140.0f) fShowMainMenu = false;
       }        
    }





    void RenderFrame() {
        // =========================================================================
        // 1. STEP POSIX TIMING ENGINES AND KERNEL RECORD SAMPLES
        // =========================================================================
        SyncDockWithRunningDeskbarApps(); // Rebuilds fTaskbarWindows using real OS states!

    	UpdateLiveClockTexture();
    	UpdateGlobalCpuLoadTracker();
        
        
        UpdateLiveClockTexture();
        UpdateGlobalCpuLoadTracker(); // Pull processor ticks and update our 40-cell buffer array

        glClearColor(fBgColorR, fBgColorG, fBgColorB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // =========================================================================
        // 2. FULLSCREEN WALLPAPER DRAW PASS (ASPECT-ALIGNED)
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
        // 3. TASKBAR-ENABLED DOCK WIDTH GEOMETRY CALCULATIONS
        // =========================================================================
        float baseSize = 48.0f;
        float padding = 12.0f;
        
        // Dynamic Allocation: Base Leaf Menu + Active Desktop File Vectors Count
        size_t baselineLaunchersCount = fDesktopItems.size() + 1; 

        // Filter out closed window nodes to find how many taskbar icons need to be painted
        size_t activeWindowsCount = 0;
        for (const auto& w : fTaskbarWindows) {
            if (*w.openStateFlag == true) activeWindowsCount++;
        }

        // Total Icon slots required on our dynamic panel layout
        size_t totalIconsCount = baselineLaunchersCount + activeWindowsCount;

        std::vector<float> dynamicWidths;
        std::vector<float> dynamicScales;
        float totalDockWidth = 0.0f;
        float maxDockHeight = baseSize;

        for (size_t i = 0; i < totalIconsCount; ++i) {
            float approximateIconX = (fWidth / 2.0f) - ((totalIconsCount * (baseSize + padding)) / 2.0f) + (i * (baseSize + padding)) + (baseSize / 2.0f);
            float distanceX = std::abs(fMouseX - approximateIconX);
            
            float scale = 1.0f;
            if (fMouseY >= (fHeight - 140.0f)) {
                if (distanceX < 160.0f) {
                    float ratio = distanceX / 160.0f;
                    scale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
                }
            }

            float finalSize = baseSize * scale;
            dynamicWidths.push_back(finalSize);
            dynamicScales.push_back(scale);
            totalDockWidth += finalSize + padding;
            if (finalSize > maxDockHeight) maxDockHeight = finalSize;
        }
        totalDockWidth -= padding;

        // Custom segment dividers spacing values
        float clockSectionPadding = 24.0f;
        float cpuGraphWidth = 60.0f;
        float separatorGapPadding = 16.0f; // Sizing space between desktop links and live open apps

        // =========================================================================
        // UNIFIED PREDICTIVE TRAY GEOMETRY LOOP (FIX FOR RIGHT-EDGE CLIPPING)
        // =========================================================================
        // We simulate the exact progressive currentX increments from the render pass
        // to find the precise real-time width required for the clock, trash, and CPU graph.
        float baseTrashSize = 48.0f; // Synchronized full scale match!
        float trashScale = 1.0f;
        float currentPredictX = 20.0f; 
        
        // 1. Accumulate dynamic width of left shortcuts zone
        currentPredictX += totalDockWidth;
        if (activeWindowsCount > 0) {
            currentPredictX += separatorGapPadding;
        }
        
        float traySectionStartX = currentPredictX;

        // 2. Clock layout calculation
        if (fClockTexture.id != 0) {
            currentPredictX += clockSectionPadding;
            currentPredictX += fClockWidth;
        }

        // 3. Trash magnification math linked to the real rendering path
        currentPredictX += clockSectionPadding;
        float approximateTrashX = (fWidth / 2.0f) - ((totalDockWidth + (currentPredictX + (baseTrashSize / 2.0f) - traySectionStartX)) / 2.0f) + currentPredictX + (baseTrashSize / 2.0f);
        float distanceTrashX = std::abs(fMouseX - approximateTrashX);

        if (fMouseY >= (fHeight - 140.0f)) {
            if (distanceTrashX < 160.0f) {
                float ratio = distanceTrashX / 160.0f;
                trashScale = 1.0f + (1.8f - 1.0f) * std::exp(-ratio * ratio);
            }
        }
        float dynamicTrashSize = baseTrashSize * trashScale;
        
        // Update backplate height constraints if the trash grows larger than the app shortcuts
        if (dynamicTrashSize > maxDockHeight) {
            maxDockHeight = dynamicTrashSize;
        }
        currentPredictX += dynamicTrashSize;

        // 4. CPU Graph layout calculation
        currentPredictX += clockSectionPadding;
        currentPredictX += cpuGraphWidth;

        float finalTrayWidth = currentPredictX - traySectionStartX;
        float finalPlateWidth = totalDockWidth + finalTrayWidth + clockSectionPadding;
        if (activeWindowsCount > 0) {
            finalPlateWidth += separatorGapPadding;
        }

        float dockMarginBottom = 15.0f;
        HaikuRect dockPlate;
        dockPlate.left = (fWidth / 2.0f) - (finalPlateWidth / 2.0f) - 20.0f;
        dockPlate.right = (fWidth / 2.0f) + (finalPlateWidth / 2.0f) + 20.0f;
        dockPlate.bottom = fHeight - dockMarginBottom;
        dockPlate.top = dockPlate.bottom - maxDockHeight - 20.0f;

        // Lock down the definitive global boundaries for rendering AND click-handling
        float trashTop = dockPlate.bottom - 10.0f - dynamicTrashSize;
        fTrashRect.left = dockPlate.left + 20.0f + totalDockWidth;
        if (activeWindowsCount > 0) {
            fTrashRect.left += separatorGapPadding;
        }
        fTrashRect.left += clockSectionPadding + (fClockTexture.id != 0 ? fClockWidth + clockSectionPadding : 0);
        fTrashRect.right = fTrashRect.left + dynamicTrashSize;
        fTrashRect.top = trashTop;
        fTrashRect.bottom = dockPlate.bottom - 10.0f;

        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float cornerRadius = 15.0f;
        DrawFilledRoundedRect(dockPlate, cornerRadius, 0.95f, 0.95f, 0.95f, 0.4f); 
        DrawOutlineRoundedRect(dockPlate, cornerRadius, 0.15f, 0.15f, 0.15f, 0.8f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);




        // =========================================================================
        // 4. RENDER DOCK BACKPLATE SHELF
        // =========================================================================

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);        
        DrawFilledRoundedRect(dockPlate, cornerRadius, 0.95f, 0.95f, 0.95f, 0.4f); 
        DrawOutlineRoundedRect(dockPlate, cornerRadius, 0.15f, 0.15f, 0.15f, 0.8f);
        
        // Reset global alpha tint filter so icons draw with full brightness
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
		    glLineWidth(2.0f); glColor4f(0.15f, 0.15f, 0.15f, 0.5f); // Matte Charcoal split divider
		    glBegin(GL_LINES);
		        glVertex2f(currentX, dockPlate.top + 6.0f);
		        glVertex2f(currentX, dockPlate.bottom - 6.0f);
		    glEnd();
		    currentX += (separatorGapPadding / 2.0f) + padding;
		}
		
		// STEP 3: RENDER THE LIVE RUNNING APPLICATION WINDOW CONTEXT TAIL LOG MODULES
		for (size_t w = 0; w < fTaskbarWindows.size(); ++w) {
		    auto& activeTaskWin = fTaskbarWindows[w];		
		    float size = dynamicWidths[renderingSlotIdx];
		    HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
		
		
            if (activeTaskWin.title == "Trackerx") {
                // TRACKER EXCLUSIVITY: Preserve your original working strategy.
                // Do not override 'isMinimized' using global counts or cached threads.
                // Let your verified click-handling states manage Tracker's visibility.
            } 
            else {
				
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
                }
                
                // Check system-wide foreground focus
                app_info activeAppInfo;
                bool isCurrentlyForeground = false;
                if (be_roster->GetActiveAppInfo(&activeAppInfo) == B_OK) {
                    if (activeAppInfo.team == activeTaskWin.teamId) {
                        isCurrentlyForeground = true;
                    }
                }

                // --- BALANCED ASYMMETRICAL LATCH SAFETY GUARD ---
                static std::map<team_id, int32> invisibleStreak;
                static std::map<team_id, int32> visibleStreak;
                
                // ASYMMETRICAL BALANCING:
                const int32 THRESHOLD_MINIMIZE = 1; 
                const int32 THRESHOLD_MAXIMIZE = 33; 

                bool nextState = activeTaskWin.isMinimized;

                if (isCurrentlyForeground) {
                    invisibleStreak[activeTaskWin.teamId] = 0;
                    visibleStreak[activeTaskWin.teamId] = 0;
                    nextState = false;
                } else {
                    if (!generalAppIsVisible) {
                        invisibleStreak[activeTaskWin.teamId]++;
                        visibleStreak[activeTaskWin.teamId] = 0;
                        
                        bool quickBackgroundCheck = false;
                        if (activeAppInfo.team == activeTaskWin.teamId) {
                            quickBackgroundCheck = (activeAppInfo.flags & B_BACKGROUND_APP);
                        }

                        if (quickBackgroundCheck || invisibleStreak[activeTaskWin.teamId] >= THRESHOLD_MINIMIZE) {
                            nextState = true;
                        }
                    } else {
                        visibleStreak[activeTaskWin.teamId]++;
                        invisibleStreak[activeTaskWin.teamId] = 0;
                        
                        // Icon will only un-dim if the app threads stay awake continuously 
                        // for 20 frames, filtering out temporary spikes completely.
                        if (visibleStreak[activeTaskWin.teamId] >= THRESHOLD_MAXIMIZE) {
                            nextState = false;
                        }
                    }
                }

                // Smoothly toggle the state for non-Tracker applications
                activeTaskWin.isMinimized = nextState;

          
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
		    if (activeTaskWin.isMinimized == false) {
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
		



        // =========================================================================
        // 6. DRAW SYSTEM CLOCK STATUS TEXT
        // =========================================================================
        if (fClockTexture.id != 0) {
            currentX += clockSectionPadding - padding;
            float plateHeight = dockPlate.bottom - dockPlate.top;
            float clockY = dockPlate.top + (plateHeight / 2.0f) - (fClockHeight / 2.0f);
            HaikuRect clockB = { currentX, clockY, currentX + static_cast<float>(fClockWidth), clockY + static_cast<float>(fClockHeight) };
            
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, fClockTexture.id);
            glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(clockB.left, clockB.top);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(clockB.right, clockB.top);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(clockB.right, clockB.bottom);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(clockB.left, clockB.bottom);
            glEnd();
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

            // Vertical 1-pixel separator line splitting clock from icon lists
            glLineWidth(1.0f); glColor4f(0.15f, 0.15f, 0.15f, 0.3f);
            glBegin(GL_LINES);
                glVertex2f(currentX - (clockSectionPadding / 2.0f), dockPlate.top + 8.0f);
                glVertex2f(currentX - (clockSectionPadding / 2.0f), dockPlate.bottom - 8.0f);
            glEnd();
            currentX += fClockWidth;
        }
        
        // =========================================================================
        // 6C. DRAW HAIKU TRASH BIN (RIGHT OF THE CLOCK)
        // =========================================================================
        // THROTTLED FILE SYSTEM SCANNER: Automatically re-query live node twice per second
        uint32 currentTicks = SDL_GetTicks();
        if (currentTicks - fLastTrashCheckTime > 500) { 
            fLastTrashCheckTime = currentTicks;
            if (fHaikuTrashIcon.id != 0) {
                glDeleteTextures(1, &fHaikuTrashIcon.id);
                fHaikuTrashIcon.id = 0;
            }
            fHaikuTrashIcon = LoadIconFromNode("/boot/trash", 128);
        }

        // RENDER PASS: Clean drawing via locked rect.
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

        // =========================================================================
        // MATCHING HOVER PROXIMITY TEST AND DIRECT GUIDANCE TEXT LAYER
        // =========================================================================
        if (fTrashRect.Contains(fMouseX, fMouseY)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // 1. Re-render texture asset ONCE upon hover entry to protect frame rates
            if (!fTrashTextGenerated) {
                if (fTrashTooltipTexId != 0) {
                    glDeleteTextures(1, &fTrashTooltipTexId);
                    fTrashTooltipTexId = 0;
                }
                // Fetch high-fidelity text string texture using your exact internal function
                // We extract the underlying '.id' from whatever layout object type it returns!
                auto generatedTex = RenderTextToTexture("Right click to empty Trash", &fTrashTooltipW, &fTrashTooltipH);
                fTrashTooltipTexId = generatedTex.id; 
                fTrashTextGenerated = true; 
            }

            // 2. Position a miniature floating background tooltip box right above the Trash node
            float tooltipW = static_cast<float>(fTrashTooltipW) + 12.0f;
            float tooltipH = static_cast<float>(fTrashTooltipH) + 8.0f;
            float tooltipLeft = fTrashRect.left + ((fTrashRect.right - fTrashRect.left) / 2.0f) - (tooltipW / 2.0f);
            float tooltipBottom = fTrashRect.top - 8.0f; // Symmetrical 8-pixel floating layout gap

            HaikuRect tooltipBounds = { 
                tooltipLeft, 
                tooltipBottom - tooltipH, 
                tooltipLeft + tooltipW, 
                tooltipBottom 
            };

            // Draw translucent charcoal indicator plate backing matching the CPU widget (75% opacity)
            DrawFilledRect(tooltipBounds, 0.15f, 0.15f, 0.15f, 0.75f);

            // Draw crisp perimeter border outline around tooltip card
            glColor4f(0.10f, 0.10f, 0.10f, 0.5f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(tooltipBounds.left,  tooltipBounds.top);   glVertex2f(tooltipBounds.right, tooltipBounds.top);
                glVertex2f(tooltipBounds.right, tooltipBounds.bottom); glVertex2f(tooltipBounds.left,  tooltipBounds.bottom);
            glEnd();

            // 3. Map the font text layer directly inside the card (Render clean white lettering)
            if (fTrashTooltipTexId != 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, fTrashTooltipTexId);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // High-contrast solid white print

                float textX = tooltipBounds.left + 6.0f;
                float textY = tooltipBounds.top + 4.0f;

                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textX, textY);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textX + fTrashTooltipW, textY);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textX + fTrashTooltipW, textY + fTrashTooltipH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textX, textY + fTrashTooltipH);
                glEnd();

                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);
            }
            glDisable(GL_BLEND);
        } else {
            // Reset state latch toggle when mouse departs bounds so it can regenerate text when needed
            fTrashTextGenerated = false;
        }

        // 1-pixel divider splitting the clock from our trash segment
        glLineWidth(1.0f); glColor4f(0.15f, 0.15f, 0.15f, 0.3f);
        glBegin(GL_LINES);
            glVertex2f(fTrashRect.left - (clockSectionPadding / 2.0f), dockPlate.top + 8.0f);
            glVertex2f(fTrashRect.left - (clockSectionPadding / 2.0f), dockPlate.bottom - 8.0f);
        glEnd();
        
        // Update currentX using the right edge of the trash bounds to safely feed the CPU graph position
        currentX = fTrashRect.right;




        

        // =========================================================================
        // 6B. DRAW GRAPHICAL GREEN LED HISTORICAL CPU MONITOR & HOVER TOOLTIP
        // =========================================================================
        currentX += clockSectionPadding;
        float graphHeight = 28.0f;
        float graphTop = dockPlate.top + ((dockPlate.bottom - dockPlate.top) / 2.0f) - (graphHeight / 2.0f);
        HaikuRect cpuGraphBounds = { currentX, graphTop, currentX + cpuGraphWidth, graphTop + graphHeight };

        // 1. Draw solid dark background casing container
        DrawFilledRect(cpuGraphBounds, 0.05f, 0.10f, 0.05f, 0.9f); 
        
        // 2. Loop through the historical data array to paint individual glowing LED columns
        float columnWidth = cpuGraphWidth / 40.0f;
        glBegin(GL_QUADS);
        for (int i = 0; i < 40; ++i) {
            int bufferIndex = (fCpuHistoryIndex + i) % 40;
            float activeLoadFactor = fCpuHistory[bufferIndex];
            
            float barLeft = cpuGraphBounds.left + (i * columnWidth) + 0.5f;
            float barRight = barLeft + columnWidth - 0.5f;
            float barTop = cpuGraphBounds.bottom - (activeLoadFactor * graphHeight);
            
            // Set bright glowing Green LED columns
            glColor4f(0.2f, 0.95f, 0.2f, 0.85f);
            glVertex2f(barLeft,  barTop);
            glVertex2f(barRight, barTop);
            glVertex2f(barRight, cpuGraphBounds.bottom);
            glVertex2f(barLeft,  cpuGraphBounds.bottom);
        }
        glEnd();

        // 3. 1-pixel dark framing edge line outline around the display case
        glColor4f(0.15f, 0.15f, 0.15f, 0.6f);
        glBegin(GL_LINE_LOOP);
            glVertex2f(cpuGraphBounds.left, cpuGraphBounds.top);     glVertex2f(cpuGraphBounds.right, cpuGraphBounds.top);
            glVertex2f(cpuGraphBounds.right, cpuGraphBounds.bottom); glVertex2f(cpuGraphBounds.left, cpuGraphBounds.bottom);
        glEnd();

        // Divider splitting the clock from our graph panel segment
        glBegin(GL_LINES);
            glVertex2f(currentX - (clockSectionPadding / 2.0f), dockPlate.top + 8.0f);
            glVertex2f(currentX - (clockSectionPadding / 2.0f), dockPlate.bottom - 8.0f);
        glEnd();

        // =========================================================================
        // ADDED: HOVER PROXIMITY TEST AND DYNAMIC PERCENTAGE TEXT LAYER
        // =========================================================================
        if (cpuGraphBounds.Contains(fMouseX, fMouseY)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // 1. Fetch the absolute latest percentage stored in our history queue
            int latestIndex = (fCpuHistoryIndex == 0) ? 39 : fCpuHistoryIndex - 1;
            int cpuPercent = static_cast<int>(fCpuHistory[latestIndex] * 100.0f);

            // Format a clean label reading text (e.g., "CPU: 42%")
            char textBuffer[32];
            snprintf(textBuffer, sizeof(textBuffer), "CPU: %d%%", cpuPercent);
            std::string currentTooltipStr(textBuffer);

            // 2. Optimization check: Only rebuild text asset if the value changed
            if (currentTooltipStr != fLastCpuTooltipStr) {
                if (fCpuTooltipTex.id != 0) {
                    glDeleteTextures(1, &fCpuTooltipTex.id);
                    fCpuTooltipTex.id = 0;
                }
                fLastCpuTooltipStr = currentTooltipStr;
                fCpuTooltipTex = RenderTextToTexture(fLastCpuTooltipStr.c_str(), &fCpuTooltipW, &fCpuTooltipH);
            }

            // 3. Position a miniature floating background tooltip box right above the LED display
            float tooltipW = static_cast<float>(fCpuTooltipW) + 12.0f;
            float tooltipH = static_cast<float>(fCpuTooltipH) + 8.0f;
            float tooltipLeft = cpuGraphBounds.left + (cpuGraphBounds.Width() / 2.0f) - (tooltipW / 2.0f);
            float tooltipBottom = cpuGraphBounds.top - 8.0f; // 8-pixel floating layout gap

            HaikuRect tooltipBounds = { 
                tooltipLeft, 
                tooltipBottom - tooltipH, 
                tooltipLeft + tooltipW, 
                tooltipBottom 
            };

            // Draw translucent charcoal indicator plate backing
            DrawFilledRect(tooltipBounds, 0.15f, 0.15f, 0.15f, 0.75f); // 75% dark matte glass

            // Draw crisp perimeter border outline around tooltip card
            glColor4f(0.10f, 0.10f, 0.10f, 0.5f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(tooltipBounds.left,  tooltipBounds.top);   glVertex2f(tooltipBounds.right, tooltipBounds.top);
                glVertex2f(tooltipBounds.right, tooltipBounds.bottom); glVertex2f(tooltipBounds.left,  tooltipBounds.bottom);
            glEnd();

            // 4. Map the font text layer directly inside the card (Render clean white lettering)
            if (fCpuTooltipTex.id != 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, fCpuTooltipTex.id);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // High-contrast solid white print

                float textX = tooltipBounds.left + 6.0f;
                float textY = tooltipBounds.top + 4.0f;

                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textX, textY);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textX + fCpuTooltipW, textY);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textX + fCpuTooltipW, textY + fCpuTooltipH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textX, textY + fCpuTooltipH);
                glEnd();

                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);
            }
        }
        glDisable(GL_BLEND);       

    } // Exact functional closing brace of RenderFrame() method!




private:

    void UpdateGlobalCpuLoadTracker() {
        uint32 currentTicks = SDL_GetTicks();
        // Check if 1 second (1000ms) has passed since our last pulse sample
        if (currentTicks - fLastCpuPulseTime < 1000) return;
        fLastCpuPulseTime = currentTicks;

        system_info info;
        if (get_system_info(&info) != B_OK) return;

        cpu_info* cpuInfos = new cpu_info[info.cpu_count];
        if (get_cpu_info(0, info.cpu_count, cpuInfos) != B_OK) {
            delete[] cpuInfos;
            return;
        }

        bigtime_t currentActive = 0;
        for (uint32_t i = 0; i < info.cpu_count; ++i) {
            currentActive += cpuInfos[i].active_time;
        }
        delete[] cpuInfos;

        bigtime_t currentTotal = system_time() * info.cpu_count;

        bigtime_t activeDelta = currentActive - fPrevActiveTicks;
        bigtime_t totalDelta = currentTotal - fPrevTotalTicks;

        float combinedLoad = 0.0f;
        if (totalDelta > 0) {
            combinedLoad = static_cast<float>(activeDelta) / static_cast<float>(totalDelta);
        }

        // Clamp the float safely between a normalized 0.0 and 1.0 boundary socket range
        if (combinedLoad < 0.0f) combinedLoad = 0.0f;
        if (combinedLoad > 1.0f) combinedLoad = 1.0f;

        // Cache parameters for the next frame delta pulse cycle calculation
        fPrevActiveTicks = currentActive;
        fPrevTotalTicks = currentTotal;

        // Push structural results into our historical graph line buffer array
        fCpuHistory[fCpuHistoryIndex] = combinedLoad;
        fCpuHistoryIndex = (fCpuHistoryIndex + 1) % 40;
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
        // 1. Fetch current local system hour and minute metrics (POSIX global functions)
        time_t rawTime = ::time(nullptr);
        struct tm* timeInfo = ::localtime(&rawTime);
        if (!timeInfo) return;

        // 2. Format a traditional AM/PM time display string
        char timeBuffer[32];
        ::strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", timeInfo);
        std::string currentTimeStr(timeBuffer);

        // Strip leading zeros for a cleaner, native look (e.g., "05:30" -> "5:30")
        if (!currentTimeStr.empty() && currentTimeStr[0] == '0') {
            currentTimeStr.erase(0, 1);
        }

        // 3. Performance Check: Only rebuild the texture when a minute ticks over!
        if (currentTimeStr != fLastClockTimeString) {
            // Delete the old text texture handle from GPU VRAM if it exists
            if (fClockTexture.id != 0) {
                glDeleteTextures(1, &fClockTexture.id);
                fClockTexture.id = 0;
            }

            fLastClockTimeString = currentTimeStr;
            
            // Rasterize the fresh time stamp into an anti-aliased OpenGL text map
            fClockTexture = RenderTextToTexture(fLastClockTimeString.c_str(), &fClockWidth, &fClockHeight);
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

    HaikuTexture RenderTextToTexture(const char* labelText, int* outWidth, int* outHeight) {
        HaikuTexture textTex;
        
        // 1. Fetch system font preferences and text metrics
        float stringPixelWidth = be_plain_font->StringWidth(labelText);
        font_height fontMetrics;
        be_plain_font->GetHeight(&fontMetrics);
        float fontTotalHeight = fontMetrics.ascent + fontMetrics.descent + fontMetrics.leading;

        int bitmapW = (int)(stringPixelWidth + 6.0f);
        int bitmapH = (int)(fontTotalHeight + 4.0f);
        
        if (bitmapW % 2 != 0) bitmapW++;

        *outWidth = bitmapW;
        *outHeight = bitmapH;

        // 2. Allocate an offscreen bitmap surface layer with an alpha channel
        BRect drawingBounds(0, 0, bitmapW - 1, bitmapH - 1);
        BBitmap* textBitmap = new BBitmap(drawingBounds, B_RGBA32, true);
        
        // Explicitly clear the raw bits in memory to 0 (fully transparent black: 0x00000000)
        // This ensures absolutely zero background artifacts are left behind before rendering!
        memset(textBitmap->Bits(), 0, textBitmap->BitsLength());

        BView* drawTarget = new BView(drawingBounds, "text_raster_view", B_FOLLOW_NONE, B_WILL_DRAW);
        textBitmap->AddChild(drawTarget);

        if (textBitmap->Lock()) {
            // HighColor is our text color (Crisp White)
            drawTarget->SetHighColor(255, 255, 255, 255); 
            
            // LowColor is our transparent background color (Alpha = 0)
            drawTarget->SetLowColor(0, 0, 0, 0); 
            
            // Set drawing mode to support alpha blending overlays
            drawTarget->SetDrawingMode(B_OP_ALPHA);
            drawTarget->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
            
            drawTarget->SetFont(be_plain_font);
            
            // Draw text aligned with the baseline metrics
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

        // Upload using GL_BGRA matching native Little-Endian memory layout
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
            glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, targetSize, targetSize, 0, GL_BGRA, GL_UNSIGNED_BYTE, haikuBitmap->Bits());
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
  
    std::string fCurrentPath;
    std::vector<BrowserFileItem> fBrowserItems;
    
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

    bool  fWindowIsMaximized = false;
    float fSavedWindowX = 300.0f;
    float fSavedWindowY = 200.0f;
    float fSavedWindowW = 450.0f;
    float fSavedWindowH = 300.0f;
    
    std::vector<TaskbarItem> fTaskbarWindows;
    HaikuRect fTrashRect; 
    
    uint32 fLastTrashCheckTime;   
    float fTrashTooltipAlpha = 0.0f; 
    
    unsigned int fTrashTooltipTexId = 0; 
    int          fTrashTooltipW = 0;
    int          fTrashTooltipH = 0;
    bool         fTrashTextGenerated = false;
    
//@private    
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

    int screenWidth  = currentDisplayMode.w;
    int screenHeight = currentDisplayMode.h;
    int dockPanelW = screenWidth;
    int dockPanelH = 160; 
    int dockPanelX = 0;
    int dockPanelY = screenHeight - dockPanelH;

    SDL_Window* window = SDL_CreateWindow(
        "Haiku Desktop Taskbar Overlay Component",
        dockPanelX, dockPanelY,
        dockPanelW, dockPanelH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP
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
    glViewport(0, 0, screenWidth, 160);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // --- WALLPAPER RE-STITCH ALIGNMENT MATH ---
    float panelTopY    = static_cast<float>(screenHeight) - 160.0f;
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
    const char* localVersion = "v1.0.5"; 
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
    bool needsRender = true; // Force initial draw pass when booting up

    while (appExecuting) {
        // 1. Put the thread to complete sleep until an OS input event arrives!
        // Wakes up instantly on movement, or falls through on a 30ms timer heartbeat
        // to check if clock strings or processor metrics updated.
        if (SDL_WaitEventTimeout(&incomingEventPackage, 30)) {
            do {
                if (incomingEventPackage.type == SDL_QUIT) {
                    appExecuting = false;
                }
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

                    // =========================================================================
                    // COORDINATE MAPPING FIX: RESTORE FULLSCREEN Y MATRIX INTERACTION
                    // =========================================================================
                    // Because our local window sits at the bottom of the monitor, we add the 
                    // hidden screen offset back to mouseY so our engine's internal checks pass!
                    int hiddenScreenOffset = screenHeight - 160; 
                    int adjustedMouseY = mouseY + hiddenScreenOffset;
                    // =========================================================================

                    // Forward the adjusted coordinate down to the engine logic
                    desktopEngine.HandleMouseInput(mouseX, adjustedMouseY, buttons);

                    // UPDATED: Allow both LEFT and RIGHT clicks to pass into our router channels
                    if (incomingEventPackage.type == SDL_MOUSEBUTTONDOWN) {
                        if (incomingEventPackage.button.button == SDL_BUTTON_LEFT || 
                            incomingEventPackage.button.button == SDL_BUTTON_RIGHT) {
                            
                            // Pass coordinates and the explicit button ID down as the third parameter
                            desktopEngine.HandleMouseClick(mouseX, adjustedMouseY, incomingEventPackage.button.button);
                        }
                    }
                    
                    needsRender = true; // Mouse moved or clicked -> Schedule a redraw pass!
                }

                else if (incomingEventPackage.type == SDL_MOUSEWHEEL) {
                    desktopEngine.HandleMouseWheel(incomingEventPackage.wheel.y);
                    needsRender = true; // Scrolled tracker layout -> Schedule redraw pass
                }        
            } while (SDL_PollEvent(&incomingEventPackage)); 
        }

        uint32 currentTime = SDL_GetTicks();


        // 2. Rate-limit structural background system updates (Clock & CPU counters)
        if (currentTime - lastMetricsUpdateTime >= 1000 || lastMetricsUpdateTime == 0) {
            // Your clock changes once a minute and CPU changes once a second. 
            // Running this once per second is more than enough.
            lastMetricsUpdateTime = currentTime;
            needsRender = true; // Tell engine to paint the new clock/graph values
        }

        // 3. Rate-limit your native Haiku team roster process loop
        if (currentTime - lastRosterScanTime >= 400 || lastRosterScanTime == 0) {
            desktopEngine.SyncDockWithRunningDeskbarApps();
            lastRosterScanTime = currentTime;
            needsRender = true; // App opened/closed -> Schedule layout recalculation redraw
        }

        // 4. ONLY EXECUTE GRAPHICS CALCULATIONS IF SOMETHING ACTUALLY CHANGED!
        if (needsRender) {
            desktopEngine.RenderFrame();
            SDL_GL_SwapWindow(window);
            needsRender = false; // Reset draw trigger flag cleanly
        }
    }

    // Explicit Context Resource Destruction sequence
    std::cout << "[System Terminal] Closing hDesktop context cleanly." << std::endl;
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
