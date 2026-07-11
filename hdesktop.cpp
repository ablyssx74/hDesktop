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
        std::cout << "[Native Window] Destructor executing. Resetting state handle..." << std::endl;
        gActiveDrawerInstance = nullptr;
    }
};

//-----------------------------------------------------      

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
    bool isMinimized;        // We will update this dynamically using the real app state!
    bool* openStateFlag;     
    bool* minimizeStateFlag; 
    team_id teamId;          // ADDED: Track the real native Haiku Process Team ID
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
    	fFBOSizeW = width;
        fFBOSizeH = height;
        fBgColorR = 0.20f; fBgColorG = 0.42f; fBgColorB = 0.58f; 
        
        fWindowPos = { 300.0f, 200.0f }; // Places the window cleanly on screen!
        fIsDragging = false;
        fDragOffsetX = 0.0f;
        fDragOffsetY = 0.0f;

        // Define initial window dimensions
        fWindowWidth = 450.0f;
        fWindowHeight = 300.0f;
        fTabWidth = 140.0f;  // Shorter yellow tab width to mimic "ScreenSaver" proportions
        fTabHeight = 21.0f;  // Sleeker vertical height matching the native decorator

        fMouseX = 0;
        fMouseY = 0;
        
        fTrashTooltipTexId = 0; // FIXED name parameter match
		fTrashTextGenerated = false;
		fTrashTooltipW = 0;
		fTrashTooltipH = 0;

        fHaikuMenuIcon = LoadIconFromNode("/boot/system/apps/AboutSystem", 128);
        fHaikuTrashIcon = LoadIconFromNode("/boot/trash", 128);
        //fWallpaperTexture = LoadWallpaperViaTranslationKit("/boot/system/data/hdesktop/background/background.png");
        
        BString capturedWallpaper = GetActiveHaikuWallpaperPath();
        fWallpaperTexture = LoadWallpaperViaTranslationKit(capturedWallpaper.String());
		
		status_t result = ForceActiveWallpaperMode(3);
        // =========================================================================
        // INITIALIZE SYSTEM MONITOR AND WINDOW STATE UTILITIES
        // =========================================================================
        fWindowIsOpen = true;
        fWindowIsMinimized = false;
        fLastCpuPulseTime = SDL_GetTicks();

        fNavigatorIcon = LoadIconFromNode("/boot/system/preferences/Tracker", 128);
        //fNavigatorText = RenderTextToTexture("Navigator", &fNavigatorTextW, &fNavigatorTextH);

        for (int i = 0; i < 40; ++i) {
            fCpuHistory[i] = 0.0f;
        }

        system_info info;
        if (get_system_info(&info) == B_OK) {
            fPrevTotalTicks = 0;
            fPrevActiveTicks = 0;
            cpu_info* cpuInfos = new cpu_info[info.cpu_count];
            if (get_cpu_info(0, info.cpu_count, cpuInfos) == B_OK) {
                // FIX: Changed 'i' to 'c' to match the loop control variable
                for (uint32_t c = 0; c < info.cpu_count; ++c) {
                    fPrevActiveTicks += cpuInfos[c].active_time;
                }
            }
            delete[] cpuInfos;
        }

		  /*      
        // =========================================================================
        // READ THE LIVE HAIKU DESKTOP DIRECTORY ENTRIES FOR THE DOCK
        // =========================================================================
        BDirectory desktopDir("/boot/home/Desktop");
        if (desktopDir.InitCheck() == B_OK) {
            BEntry entry;
            int iconDimension = 128; // Base icon size

            while (desktopDir.GetNextEntry(&entry) == B_OK) {
                DesktopIconItem item;                
                char nameBuf[B_FILE_NAME_LENGTH];
                entry.GetName(nameBuf);
                item.name = nameBuf;
                item.isFolder = entry.IsDirectory();
                BPath path;
                entry.GetPath(&path);                
                item.texture = LoadIconFromNode(path.Path(), iconDimension);                
                int textW = 0, textH = 0;
                item.textTexture = RenderTextToTexture(item.name.c_str(), &textW, &textH);
                item.bounds = { 0.0f, 0.0f, 0.0f, 0.0f };
                item.textBounds = { 0.0f, 0.0f, 0.0f, 0.0f };                
                fDesktopItems.push_back(item);
            }
        }
        */
        // =========================================================================
        // INITIALIZE LIVE FILESYSTEM TRACKER BROWSER WINDOW
        // =========================================================================
        RefreshWindowDirectory("/boot/home");

        
        // =========================================================================
        // INITIALIZE CUSTOM START MENU TYPOGRAPHY LABELS
        // =========================================================================
        fMenuTextApps     = RenderTextToTexture("Applications", &fMenuTextAppsW, &fMenuTextAppsH);
        fMenuTextPrefs    = RenderTextToTexture("Preferences",  &fMenuTextPrefsW,  &fMenuTextPrefsH);
        fMenuTextShutdown = RenderTextToTexture("Exit",   &fMenuTextShutdownW, &fMenuTextShutdownH);


        // =========================================================================
        // INITIALIZE ACTIVE WINDOW NODES INTO THE TASKBAR VECTOR ARRAY
        // =========================================================================
        TaskbarItem navTask;
        navTask.title = "Navigator";
        navTask.icon = fNavigatorIcon; // Borrow the sharp Tracker dog asset texture
        navTask.openStateFlag = &fWindowIsOpen;
        navTask.minimizeStateFlag = &fWindowIsMinimized;
        
        fTaskbarWindows.push_back(navTask);


        // =========================================================================
        // INITIALIZE OFFSCREEN CANVAS FRAMEBUFFER (FBO)
        // =========================================================================
        // This generates distinct hardware IDs so your FBO can't overwrite texture 0!
        glGenFramebuffers(1, &fWindowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, fWindowFBO);

        glGenTextures(1, &fWindowRenderTexture);
        glBindTexture(GL_TEXTURE_2D, fWindowRenderTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fFBOSizeW, fFBOSizeH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fWindowRenderTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[GL FBO ERROR] Offscreen preview surface initialization failed!" << std::endl;
        }

        // Drop the binding back to standard monitor space immediately
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
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
	        if (be_roster->GetRunningAppInfo(id, &info) == B_OK) {
	            if ((info.flags & B_BACKGROUND_APP) != 0) continue;
	            if (strcmp(info.signature, "application/x-vnd.YourCloneSignature") == 0) continue;
	
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
	
	            if (appTitle == "Deskbar" || appTitle == "app_server" || appTitle == "input_server") {
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
	    // MAIN MENU DRAWER ROW ELEMENT CLICK ROUTER
	    // =========================================================================
	    if (fShowMainMenu && fMainMenuBounds.Contains(x, y)) {
	        float stepY = 30.0f;
	        float exitRowTop = fMainMenuBounds.top + (stepY * 2.0f);
	        float exitRowBottom = exitRowTop + stepY;
	
	        if (static_cast<float>(y) >= exitRowTop && static_cast<float>(y) <= exitRowBottom) {
	            std::cout << "[Main Menu] Intercepted Exit Option Click. Posting SDL_QUIT Event..." << std::endl;
	            
	            SDL_Event quitEvent;
	            quitEvent.type = SDL_QUIT;
	            SDL_PushEvent(&quitEvent);
	            
	            BWindow* nativeWin = be_app->WindowAt(0);
	            if (nativeWin != nullptr && nativeWin->Lock()) {
	                nativeWin->SetFeel(B_NORMAL_WINDOW_FEEL);
	                nativeWin->ResizeBy(0, -220.0f);
	                nativeWin->MoveBy(0, 220.0f);
	                nativeWin->Unlock();
	            }
	            fShowMainMenu = false;
	            return;
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
	
	    bool clickIsInsideOpenMenu = false;
	    if (fShowMainMenu && fMainMenuBounds.Contains(x, y)) {
	        clickIsInsideOpenMenu = true;
	    }
	
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
                    std::cout << "[Dock Leaf] Second Leaf Click Intercepted. Closing Active Dashboard..." << std::endl;
                    
                    if (gActiveDrawerInstance->Lock()) {
                        gActiveDrawerInstance->Quit(); // Destroys window and automatically fires the destructor!
                    }
                } 
                else {
                    std::cout << "[Dock Leaf] First Leaf Click Intercepted. Invoking Native Dashboard..." << std::endl;
                    
                    gActiveDrawerInstance = new HaikuAppDrawerWindow(fHeight);
                    gActiveDrawerInstance->Show();
                }
                
                fShowMainMenu = false; 
            } else {

                // --- FIXED INTERCEPT CHANNELS ---
                // App shortcuts launchers code blocks remain untouched and clean...

                fShowMainMenu = false;
                size_t itemIdx = i - 1;
                
                BEntry entry;
                BDirectory desktopDir("/boot/home/Desktop");
                if (desktopDir.InitCheck() == B_OK && desktopDir.FindEntry(fDesktopItems[itemIdx].name.c_str(), &entry) == B_OK) {
                    entry_ref ref;
                    if (entry.GetRef(&ref) == B_OK) {
                        std::cout << "[Dock Launch] Launching system file shortcut: " << fDesktopItems[itemIdx].name << std::endl;
                        be_roster->Launch(&ref);
                    }
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
	
	        // --- CASE 1: LEFT CLICK (STANDARD OPEN ACTION) ---
	        if (button == SDL_BUTTON_LEFT) {
	            std::cout << "[Dock Tray] Trash Left-Click: Launching Tracker Window..." << std::endl;
	            std::system("/boot/system/Tracker /boot/trash &");
	            return;
	        }
	        
	        // --- CASE 2: RIGHT CLICK (NATIVE EMPTY TRASH) ---
	        else if (button == SDL_BUTTON_RIGHT) {
	            std::cout << "[Dock Tray] Trash Right-Click: Invoking Native Trash Emptying Service..." << std::endl;
	            
	            // Clean, official Haiku mechanism to purge deleted items safely.
	            std::system("trash --empty &"); 
	            fLastTrashCheckTime = 0; // Force immediate asset refresh on next update pass!
	            return;
	        }
	    }
	}



                	

    void HandleMouseInput(int x, int y, Uint32 buttonState) {
        fMouseX = x; fMouseY = y;

        float titleBarHeight = 16.0f;
        float currentTabHeight = 22.0f;

        HaikuRect activeTab = { fWindowPos.x + 4.0f, fWindowPos.y - titleBarHeight - currentTabHeight, fWindowPos.x + 4.0f + fTabWidth, fWindowPos.y - titleBarHeight };
        HaikuRect leftDrop = { fWindowPos.x - 4.0f, activeTab.top, fWindowPos.x, fWindowPos.y + 4.0f };

        bool leftButtonDown = (buttonState & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;

        if (leftButtonDown) {
            // =========================================================================
            // ACTIVE RESIZE GRAB TRACKING LOOP
            // =========================================================================
            if (fIsResizing) {
                float deltaX = x - fResizeStartX;
                float deltaY = y - fResizeStartY;
                
                fWindowWidth  = fResizeStartWidth + deltaX;
                fWindowHeight = fResizeStartHeight + deltaY;
                
                // Enforce safety floor layout dimension constraints
                if (fWindowWidth < 250.0f)  fWindowWidth = 250.0f;
                if (fWindowHeight < 180.0f) fWindowHeight = 180.0f;
                
                // Keep the max scroll track offset calculated dynamically
                float totalListHeight = fBrowserItems.size() * 30.0f;
                float canvasHeight = fWindowHeight - 4.0f - 20.0f;
                fMaxScrollOffset = totalListHeight - canvasHeight;
                if (fMaxScrollOffset < 0.0f) fMaxScrollOffset = 0.0f;
            }
            // STANDARD WINDOW DRAGGING LOOP
            else if (fIsDragging) {
                fWindowPos.x = x - fDragOffsetX;
                fWindowPos.y = y - fDragOffsetY;
            }
            else {
                // If clicked but not dragging or resizing yet, check entry vectors
                if (activeTab.Contains(x, y) || leftDrop.Contains(x, y)) {
                    fIsDragging = true;
                    fDragOffsetX = x - fWindowPos.x;
                    fDragOffsetY = y - fWindowPos.y;
                }
            }
        } else {
            fIsDragging = false;
            fIsResizing = false; // Release resize state safely when mouse is let go
        }

        // Workspace dismiss click checking for the start menu...
        if (leftButtonDown && !fIsDragging && !fIsResizing) {
            if (fShowMainMenu && !fMainMenuBounds.Contains(x, y)) {
                if (y < fHeight - 140.0f) fShowMainMenu = false;
            }
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

        // CRITICAL FIX: Base the horizontal calculation on your actual rendering start offset
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
        
       // float cornerRadius = 15.0f;
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
		    
		    // SAFE HYBRID FIX FOR TRACKER:
		    // Never skip rendering Tracker even if a generic state or global count claims it is closed.
		    // This keeps the Tracker icon anchored and prevents the cascade that terminates the desktop team.
		    if (activeTaskWin.title == "Tracker") {
		        // Enforce Tracker to always stay alive on the dock visual plane
		        *activeTaskWin.openStateFlag = true; 
		    } else {
		        // Standard check for TV, IceWeasel, and all other general applications
		        if (*activeTaskWin.openStateFlag == false) continue; 
		    }
		
		    float size = dynamicWidths[renderingSlotIdx];
		    // float scale = dynamicScales[renderingSlotIdx];
		    HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };
		
		    // A. Draw active task window application vector icon thumbnail
		    if (activeTaskWin.icon.id != 0) {
		        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, activeTaskWin.icon.id);
		        
		        // Use your stable local flag logic from Code 1 to eliminate flickering
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
        
        return;
        
        // =========================================================================
        // 11. DRAW CUSTOM INTERNAL SEMI-TRANSPARENT MAIN MENU OVERLAY WITH TEXT
        // =========================================================================
        if (fShowMainMenu) {
            // =========================================================================
            // REVERT VIEWPORT MATCH: Keep your original stable canvas scaling intact!
            // =========================================================================
            glViewport(0, 0, fWidth, fHeight); 

            // Enable blending for transparent overlay drawing layers
            glEnable(GL_BLEND); 
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Draw your semi-translucent light grey menu panel box backing
            glColor4f(0.847f, 0.847f, 0.847f, 0.85f); 
            glBegin(GL_QUADS);
                glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.top);   glVertex2f(fMainMenuBounds.right, fMainMenuBounds.top);
                glVertex2f(fMainMenuBounds.right, fMainMenuBounds.bottom); glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.bottom);
            glEnd();

            // Draw crisp dark framing perimeter border outline line around the card
            glColor4f(0.196f, 0.196f, 0.196f, 0.9f); glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.top);   glVertex2f(fMainMenuBounds.right, fMainMenuBounds.top);
                glVertex2f(fMainMenuBounds.right, fMainMenuBounds.bottom); glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.bottom);
            glEnd();

            // Draw soft upper illumination border stripe line highlight segment
            glColor4f(1.0f, 1.0f, 1.0f, 0.6f);
            glBegin(GL_LINE_STRIP);
                glVertex2f(fMainMenuBounds.left + 1.0f,  fMainMenuBounds.bottom - 1.0f);
                glVertex2f(fMainMenuBounds.left + 1.0f,  fMainMenuBounds.top + 1.0f);
                glVertex2f(fMainMenuBounds.right - 1.0f, fMainMenuBounds.top + 1.0f);
            glEnd();

            // Draw individual matte gray separator shelf divider lines
            glColor4f(0.6f, 0.6f, 0.6f, 0.4f); float stepY = 30.0f;
            glBegin(GL_LINES);
                for (float y = fMainMenuBounds.top + stepY; y < fMainMenuBounds.bottom; y += stepY) {
                    glVertex2f(fMainMenuBounds.left + 4.0f,  y); glVertex2f(fMainMenuBounds.right - 4.0f, y);
                }
            glEnd();

            glEnable(GL_TEXTURE_2D);
            glColor4f(0.0f, 0.0f, 0.0f, 1.0f); // Crisp high-contrast black font ink filter
            float textIndentX = fMainMenuBounds.left + 12.0f;

            // Map and bind Row 1 Text: Apps Option
            if (fMenuTextApps.id != 0) {
                float y1 = fMainMenuBounds.top + (stepY / 2.0f) - (fMenuTextAppsH / 2.0f);
                glBindTexture(GL_TEXTURE_2D, fMenuTextApps.id);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textIndentX, y1);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textIndentX + fMenuTextAppsW, y1);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textIndentX + fMenuTextAppsW, y1 + fMenuTextAppsH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textIndentX, y1 + fMenuTextAppsH);
                glEnd();
            }
            // Map and bind Row 2 Text: Preferences Option
            if (fMenuTextPrefs.id != 0) {
                float y2 = fMainMenuBounds.top + stepY + (stepY / 2.0f) - (fMenuTextPrefsH / 2.0f);
                glBindTexture(GL_TEXTURE_2D, fMenuTextPrefs.id);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textIndentX, y2);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textIndentX + fMenuTextPrefsW, y2);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textIndentX + fMenuTextPrefsW, y2 + fMenuTextPrefsH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textIndentX, y2 + fMenuTextPrefsH);
                glEnd();
            }
            // Map and bind Row 3 Text: Shutdown/Exit Option
            if (fMenuTextShutdown.id != 0) {
                float y3 = fMainMenuBounds.top + (stepY * 2.0f) + (stepY / 2.0f) - (fMenuTextShutdownH / 2.0f);
                glBindTexture(GL_TEXTURE_2D, fMenuTextShutdown.id);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(textIndentX, y3);
                    glTexCoord2f(1.0f, 0.0f); glVertex2f(textIndentX + fMenuTextShutdownW, y3);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(textIndentX + fMenuTextShutdownW, y3 + fMenuTextShutdownH);
                    glTexCoord2f(0.0f, 1.0f); glVertex2f(textIndentX, y3 + fMenuTextShutdownH);
                glEnd();
            }
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
        }
        else {
            // Re-enforce clean baseline layout matching dimensions when menu collapses
            glViewport(0, 0, fWidth, fHeight);
        }

        
        

		
        // =========================================================================
        // 7. DRAGGABLE & RESIZABLE WINDOW PASSTHROUGH (WITH EXTENDED CONTROLS)
        // =========================================================================
        if (fWindowIsOpen && !fWindowIsMinimized) {
            HaikuRect winWin = { fWindowPos.x, fWindowPos.y, fWindowPos.x + fWindowWidth, fWindowPos.y + fWindowHeight };
            
            float titleBarHeight = 16.0f;
            float currentTabWidth = 140.0f;
            float currentTabHeight = 22.0f;
            HaikuRect topExtenderBar = { winWin.left, winWin.top - titleBarHeight, winWin.right, winWin.top };
            HaikuRect winTab = { winWin.left + 4.0f, topExtenderBar.top - currentTabHeight, winWin.left + 4.0f + currentTabWidth, topExtenderBar.top };
            HaikuRect leftDropOverlap = { winWin.left - 4.0f, winTab.top, winWin.left, topExtenderBar.top + 4.0f };
            
            float statusBarHeight = 20.0f;
            float scrollbarWidth = 14.0f; 
            
            HaikuRect panelCanvas = { winWin.left + 4.0f, winWin.top + 4.0f, winWin.right - 4.0f - scrollbarWidth, winWin.bottom - 4.0f - statusBarHeight };
            HaikuRect statusTrayBox = { winWin.left + 4.0f, panelCanvas.bottom + 2.0f, winWin.right - 4.0f, winWin.bottom - 4.0f };

            // Draw Window Background Panels Fills
            DrawFilledRect(topExtenderBar, 0.443f, 0.474f, 0.643f); 
            DrawFilledRect(winWin, 0.847f, 0.847f, 0.847f);         
            DrawFilledRect(winTab, 1.0f, 0.796f, 0.0f);             
            DrawFilledRect(leftDropOverlap, 1.0f, 0.796f, 0.0f);    
            DrawFilledRect(panelCanvas, 0.95f, 0.95f, 0.95f);       
            DrawFilledRect(statusTrayBox, 0.847f, 0.847f, 0.847f);  

            // Purple Beveled Scrollbar Track
            HaikuRect scrollTrack = { panelCanvas.right + 2.0f, panelCanvas.top, winWin.right - 4.0f, panelCanvas.bottom };
            DrawFilledRect(scrollTrack, 0.90f, 0.90f, 0.92f);
            
            float totalListHeight = fBrowserItems.size() * 30.0f;
            float viewportHeight = panelCanvas.bottom - panelCanvas.top;
            float thumbHeight = viewportHeight;
            if (totalListHeight > 0.0f) thumbHeight = (viewportHeight / totalListHeight) * viewportHeight;
            if (thumbHeight < 20.0f) thumbHeight = 20.0f;
            if (thumbHeight > viewportHeight) thumbHeight = viewportHeight;

            float thumbTopOffset = 0.0f;
            if (fMaxScrollOffset > 0.0f) thumbTopOffset = (fScrollOffset / fMaxScrollOffset) * (viewportHeight - thumbHeight);

            HaikuRect purpleScrollThumb = { scrollTrack.left, scrollTrack.top + thumbTopOffset, scrollTrack.right, scrollTrack.top + thumbTopOffset + thumbHeight };
            DrawFilledRect(purpleScrollThumb, 0.52f, 0.44f, 0.74f); 
            HaikuRect thumbBevelInner = { purpleScrollThumb.left + 1.0f, purpleScrollThumb.top + 1.0f, purpleScrollThumb.right - 1.0f, purpleScrollThumb.bottom - 1.0f };
            DrawFilledRect(thumbBevelInner, 0.64f, 0.56f, 0.85f); 

            glColor3f(0.196f, 0.196f, 0.196f); glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(purpleScrollThumb.left, purpleScrollThumb.top); glVertex2f(purpleScrollThumb.right, purpleScrollThumb.top);
                glVertex2f(purpleScrollThumb.right, purpleScrollThumb.bottom); glVertex2f(purpleScrollThumb.left, purpleScrollThumb.bottom);
            glEnd();

            // =========================================================================
            // ADDED: DRAW CUSTOM PURPLE BEVELED CORNER GRAB-HANDLE WINDOW RESIZER
            // =========================================================================
            float handleSize = 14.0f;
            HaikuRect resizeGrip = { winWin.right - handleSize, winWin.bottom - handleSize, winWin.right, winWin.bottom };
            
            // Draw beautiful Purple-Lavender accent triangle
            glBegin(GL_TRIANGLES);
                glColor4f(0.64f, 0.56f, 0.85f, 1.0f); glVertex2f(resizeGrip.right, resizeGrip.top);
                glColor4f(0.52f, 0.44f, 0.74f, 1.0f); glVertex2f(resizeGrip.left,  resizeGrip.bottom);
                glColor4f(0.38f, 0.30f, 0.60f, 1.0f); glVertex2f(resizeGrip.right, resizeGrip.bottom);
            glEnd();

            // Fine diagonal details lines running through handle core
            glColor3f(0.196f, 0.196f, 0.196f);
            glBegin(GL_LINES);
                glVertex2f(resizeGrip.right - 4.0f, resizeGrip.top + 4.0f); glVertex2f(resizeGrip.left + 4.0f, resizeGrip.bottom - 4.0f);
                glVertex2f(resizeGrip.right - 8.0f, resizeGrip.top + 8.0f); glVertex2f(resizeGrip.left + 8.0f, resizeGrip.bottom - 8.0f);
            glEnd();

            // Directory file entries list rendering logic
            glEnable(GL_BLEND); glEnable(GL_TEXTURE_2D);
            if (fPathTextTexture.id != 0) {
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                float px = topExtenderBar.left + 8.0f; float py = topExtenderBar.top + (titleBarHeight / 2.0f) - (fPathTextH / 2.0f);
                glBindTexture(GL_TEXTURE_2D, fPathTextTexture.id);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(px, py); glTexCoord2f(1.0f, 0.0f); glVertex2f(px + fPathTextW, py);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(px + fPathTextW, py + fPathTextH); glTexCoord2f(0.0f, 1.0f); glVertex2f(px, py + fPathTextH);
                glEnd();
            }
            if (fStatusTextTexture.id != 0) {
                glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
                float sx = statusTrayBox.left + 6.0f; float sy = statusTrayBox.top + (statusBarHeight / 2.0f) - (fStatusTextH / 2.0f);
                glBindTexture(GL_TEXTURE_2D, fStatusTextTexture.id);
                glBegin(GL_QUADS);
                    glTexCoord2f(0.0f, 0.0f); glVertex2f(sx, sy); glTexCoord2f(1.0f, 0.0f); glVertex2f(sx + fStatusTextW, sy);
                    glTexCoord2f(1.0f, 1.0f); glVertex2f(sx + fStatusTextW, sy + fStatusTextH); glTexCoord2f(0.0f, 1.0f); glVertex2f(sx, sy + fStatusTextH);
                glEnd();
            }
            glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);

            glEnable(GL_SCISSOR_TEST);
            glScissor((int)panelCanvas.left, (int)(fHeight - panelCanvas.bottom), (int)panelCanvas.Width(), (int)(panelCanvas.bottom - panelCanvas.top));
            float rowY = panelCanvas.top + 6.0f - fScrollOffset;
            float rowHeight = 30.0f;

            for (size_t i = 0; i < fBrowserItems.size(); ++i) {
                auto& item = fBrowserItems[i];
                item.clickBounds = { panelCanvas.left + 2.0f, rowY, panelCanvas.right - 2.0f, rowY + rowHeight };

                if (rowY + rowHeight >= panelCanvas.top && rowY <= panelCanvas.bottom) {
                    if (item.clickBounds.Contains(fMouseX, fMouseY)) {
                        DrawFilledRect(item.clickBounds, 0.85f, 0.90f, 1.0f, 0.5f);
                    }
                    if (item.icon.id != 0) {
                        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, item.icon.id); glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                        glBegin(GL_QUADS);
                            glTexCoord2f(0.0f, 0.0f); glVertex2f(panelCanvas.left + 12.0f, rowY + 3.0f);
                            glTexCoord2f(1.0f, 0.0f); glVertex2f(panelCanvas.left + 12.0f + 24.0f, rowY + 3.0f);
                            glTexCoord2f(1.0f, 1.0f); glVertex2f(panelCanvas.left + 12.0f + 24.0f, rowY + 3.0f + 24.0f);
                            glTexCoord2f(0.0f, 1.0f); glVertex2f(panelCanvas.left + 12.0f, rowY + 3.0f + 24.0f);
                        glEnd();
                        glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                    }
                    if (item.textTex.id != 0) {
                        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, item.textTex.id); glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
                        float textYPos = rowY + (rowHeight / 2.0f) - (item.textH / 2.0f);
                        glBegin(GL_QUADS);
                            glTexCoord2f(0.0f, 0.0f); glVertex2f(panelCanvas.left + 44.0f, textYPos);
                            glTexCoord2f(1.0f, 0.0f); glVertex2f(panelCanvas.left + 44.0f + item.textW, textYPos);
                            glTexCoord2f(1.0f, 1.0f); glVertex2f(panelCanvas.left + 44.0f + item.textW, textYPos + item.textH);
                            glTexCoord2f(0.0f, 1.0f); glVertex2f(panelCanvas.left + 44.0f, textYPos + item.textH);
                        glEnd();
                        glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_TEXTURE_2D);
                    }
                }
                rowY += rowHeight;
            }
            glDisable(GL_SCISSOR_TEST);

            // =========================================================================
            // 8. HEADER CONTROLS (WITH NEW INTERMEDIATE MAXIMIZE BOX PLACEMENT)
            // =========================================================================
            float btnW = 14.0f; float btnH = 12.0f;
            float barCenterY = topExtenderBar.top + (topExtenderBar.bottom - topExtenderBar.top) / 2.0f;

            HaikuRect closeBoxBtn = { winWin.right - 6.0f - btnW, barCenterY - (btnH / 2.0f), winWin.right - 6.0f, barCenterY + (btnH / 2.0f) };
            DrawFilledRect(closeBoxBtn, 0.847f, 0.847f, 0.847f);

            // Maximize Toggle button position sitting between close and minimize controls
            HaikuRect maxToggleBtn = { closeBoxBtn.left - 4.0f - btnW, closeBoxBtn.top, closeBoxBtn.left - 4.0f, closeBoxBtn.bottom };
            DrawFilledRect(maxToggleBtn, 0.847f, 0.847f, 0.847f);

            HaikuRect zoomToggleBtn = { maxToggleBtn.left - 4.0f - btnW, maxToggleBtn.top, maxToggleBtn.left - 4.0f, maxToggleBtn.bottom };
            DrawFilledRect(zoomToggleBtn, 0.847f, 0.847f, 0.847f);

            // Charcoal Structural Trims
            glColor3f(0.196f, 0.196f, 0.196f); glLineWidth(1.0f);
            glBegin(GL_LINE_STRIP);
                glVertex2f(leftDropOverlap.left, leftDropOverlap.bottom); glVertex2f(leftDropOverlap.left, winTab.top); 
                glVertex2f(winTab.right, winTab.top); glVertex2f(winTab.right, topExtenderBar.top);
                glVertex2f(topExtenderBar.right, topExtenderBar.top); glVertex2f(winWin.right, winWin.bottom);
                glVertex2f(winWin.left, winWin.bottom); glVertex2f(winWin.left, leftDropOverlap.bottom);
            glEnd();

            glBegin(GL_LINES);
                glVertex2f(winWin.left, topExtenderBar.top); glVertex2f(winTab.right, topExtenderBar.top);
                glVertex2f(statusTrayBox.left, statusTrayBox.top); glVertex2f(statusTrayBox.right, statusTrayBox.top);
                glVertex2f(scrollTrack.left, scrollTrack.top); glVertex2f(scrollTrack.left, scrollTrack.bottom);
                glVertex2f(resizeGrip.left, resizeGrip.top); glVertex2f(resizeGrip.right, resizeGrip.top); // Separator line for resizer handle
            glEnd();

            glBegin(GL_LINE_LOOP); glVertex2f(closeBoxBtn.left, closeBoxBtn.top); glVertex2f(closeBoxBtn.right, closeBoxBtn.top); glVertex2f(closeBoxBtn.right, closeBoxBtn.bottom); glVertex2f(closeBoxBtn.left, closeBoxBtn.bottom); glEnd();
            glBegin(GL_LINE_LOOP); glVertex2f(maxToggleBtn.left,   maxToggleBtn.top);   glVertex2f(maxToggleBtn.right,   maxToggleBtn.top);   glVertex2f(maxToggleBtn.right,   maxToggleBtn.bottom);   glVertex2f(maxToggleBtn.left,   maxToggleBtn.bottom);   glEnd();
            glBegin(GL_LINE_LOOP); glVertex2f(zoomToggleBtn.left,  zoomToggleBtn.top);  glVertex2f(zoomToggleBtn.right,  zoomToggleBtn.top);  glVertex2f(zoomToggleBtn.right,  zoomToggleBtn.bottom);  glVertex2f(zoomToggleBtn.left,  zoomToggleBtn.bottom);  glEnd();

            // Inner header control markings icons lines
            glBegin(GL_LINES);
                // Close button 'X'
                glVertex2f(closeBoxBtn.left + 3.0f, closeBoxBtn.top + 2.0f); glVertex2f(closeBoxBtn.right - 3.0f, closeBoxBtn.bottom - 2.0f);
                glVertex2f(closeBoxBtn.right - 3.0f, closeBoxBtn.top + 2.0f); glVertex2f(closeBoxBtn.left + 3.0f, closeBoxBtn.bottom - 2.0f);
                // Maximize button internal box indicator frame shape
                glVertex2f(maxToggleBtn.left + 3.0f, maxToggleBtn.top + 2.0f); glVertex2f(maxToggleBtn.right - 3.0f, maxToggleBtn.top + 2.0f);
                glVertex2f(maxToggleBtn.right - 3.0f, maxToggleBtn.top + 2.0f); glVertex2f(maxToggleBtn.right - 3.0f, maxToggleBtn.bottom - 2.0f);
                glVertex2f(maxToggleBtn.right - 3.0f, maxToggleBtn.bottom - 2.0f); glVertex2f(maxToggleBtn.left + 3.0f, maxToggleBtn.bottom - 2.0f);
                glVertex2f(maxToggleBtn.left + 3.0f, maxToggleBtn.bottom - 2.0f); glVertex2f(maxToggleBtn.left + 3.0f, maxToggleBtn.top + 2.0f);
                // Minimize button single bar dash line
                glVertex2f(zoomToggleBtn.left + 3.0f, zoomToggleBtn.bottom - 4.0f); glVertex2f(zoomToggleBtn.right - 3.0f, zoomToggleBtn.bottom - 4.0f);
            glEnd();

            glColor3f(1.0f, 1.0f, 1.0f);
            glBegin(GL_LINE_STRIP);
                glVertex2f(winWin.left + 1.0f, winWin.bottom - 1.0f); glVertex2f(winWin.left + 1.0f, winWin.top + 1.0f);
                glVertex2f(winWin.right - 1.0f, winWin.top + 1.0f);
            glEnd();
            
            glColor3f(0.6f, 0.6f, 0.6f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(panelCanvas.left, panelCanvas.top); glVertex2f(panelCanvas.right, panelCanvas.top);
                glVertex2f(panelCanvas.right, panelCanvas.bottom); glVertex2f(panelCanvas.left, panelCanvas.bottom);
            glEnd();
            glDisable(GL_BLEND);
        }

    } // Exact functional closing brace of RenderFrame() method!




private:

team_id GetRealFrontTeam() {
    team_id frontTeam = -1;
    
    // We send message token 8 (AS_GET_FRONT_TEAM / AS_GET_ACTIVE_TEAM variant)
    // to query the app_server directly about the top window layout stack holder
    BPrivate::AppServerLink link;
    link.StartMessage(8); 
    link.Flush();
    
    // Read the synchronous response packet from the window manager stream
    if (link.Read<team_id>(&frontTeam) != B_OK) {
        frontTeam = -1;
    }
    
    return frontTeam;
}



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

       void RefreshWindowDirectory(const std::string& directoryPath) {
        fCurrentPath = directoryPath;
        
        // 1. Clear out old directory item text textures to prevent VRAM memory leaks
        for (auto& item : fBrowserItems) {
            if (item.textTex.id != 0) glDeleteTextures(1, &item.textTex.id);
        }
        fBrowserItems.clear();

        // Clear out old path and status textures if they exist
        if (fPathTextTexture.id != 0)   { glDeleteTextures(1, &fPathTextTexture.id); fPathTextTexture.id = 0; }
        if (fStatusTextTexture.id != 0) { glDeleteTextures(1, &fStatusTextTexture.id); fStatusTextTexture.id = 0; }

        // Reset our vertical scroll position back to the top of the pane safely!
        fScrollOffset = 0.0f;

        // 2. Open directory path nodes using Haiku's native file system engine
        BDirectory dir(fCurrentPath.c_str());
        if (dir.InitCheck() != B_OK) return;

        // Prepend parent directory ".." hop link shortcut
        if (fCurrentPath != "/") {
            BrowserFileItem parentItem;
            parentItem.name = ".. [Parent Directory]";
            parentItem.isFolder = true;
            size_t lastSlash = fCurrentPath.find_last_of('/');
            parentItem.fullPath = (lastSlash == 0) ? "/" : fCurrentPath.substr(0, lastSlash);
            parentItem.icon = LoadIconFromNode("/boot/system/preferences/Tracker", 24);
            parentItem.textTex = RenderTextToTexture(parentItem.name.c_str(), &parentItem.textW, &parentItem.textH);
            fBrowserItems.push_back(parentItem);
        }

        BEntry entry;
        int folderCount = 0;
        int fileCount = 0;

        while (dir.GetNextEntry(&entry) == B_OK) {
            BrowserFileItem item;
            char nameBuf[B_FILE_NAME_LENGTH];
            entry.GetName(nameBuf);
            item.name = nameBuf;
            item.isFolder = entry.IsDirectory();

            if (item.isFolder) folderCount++; else fileCount++;

            BPath path;
            entry.GetPath(&path);
            item.fullPath = path.Path();

            item.icon = LoadIconFromNode(item.fullPath.c_str(), 24);
            item.textTex = RenderTextToTexture(item.name.c_str(), &item.textW, &item.textH);

            fBrowserItems.push_back(item);
        }

        // =========================================================================
        // BAKE THE TITLE BAR PATH AND BOTTOM STATUS BAR TEXT TEXTURES
        // =========================================================================
        std::string pathLabel = "Path: " + fCurrentPath;
        fPathTextTexture = RenderTextToTexture(pathLabel.c_str(), &fPathTextW, &fPathTextH);

        // Format a native tracking string summary: e.g., "9 items (2 folders, 7 files)"
        char statusBuffer[128];
        snprintf(statusBuffer, sizeof(statusBuffer), "%ld items (%d folder%s, %d file%s)",
                 fBrowserItems.size(), 
                 folderCount, (folderCount == 1 ? "" : "s"), 
                 fileCount, (fileCount == 1 ? "" : "s"));
        fStatusTextTexture = RenderTextToTexture(statusBuffer, &fStatusTextW, &fStatusTextH);

        // Calculate maximum vertical scroll limits based on row distribution
        float totalListHeight = fBrowserItems.size() * 30.0f;
        float canvasHeight = fWindowHeight - 8.0f - 20.0f; // Inner box viewport height minus padding
        fMaxScrollOffset = totalListHeight - canvasHeight;
        if (fMaxScrollOffset < 0.0f) fMaxScrollOffset = 0.0f; // No scrolling needed if list fits!
    }


        void RenderWindowContentsToFBO() {
        glBindFramebuffer(GL_FRAMEBUFFER, fWindowFBO);
        glViewport(0, 0, fFBOSizeW, fFBOSizeH);

        // Clear the inside of the window texture with a clean, native light gray base surface
        glClearColor(0.847f, 0.847f, 0.847f, 1.0f); // Matched to native Haiku gray panel color
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        
        // =========================================================================
        // FIX: MATCH ORTHOGRAPHIC MATRIX PATH TO NATIVE SCREEN EXPECTATIONS
        // =========================================================================
        // Flipping the bottom/top parameters from (0, size) to (size, 0) compensates 
        // for OpenGL's default texture inversion, aligning everything with the window!
        gluOrtho2D(0.0, fFBOSizeW, fFBOSizeH, 0.0);
        
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // --- DRAW THE DYNAMIC ACTIVE GRAPHIC CONTENT ---
        float timeTick = (float)SDL_GetTicks() * 0.001f;
        
        glLineWidth(2.0f);
        glBegin(GL_LINES);
            for (int i = 0; i < fFBOSizeW; i += 40) {
                float dynamicOffset = std::sin(timeTick + (i * 0.01f)) * 30.0f; // Amplified waves!
                
                // Blue line strands
                glColor3f(0.2f, 0.5f, 0.9f); 
                glVertex2f((float)i, 0.0f);
                glVertex2f((float)i + dynamicOffset, (float)fFBOSizeH);

                // Red line strands
                glColor3f(0.9f, 0.3f, 0.3f); 
                glVertex2f(0.0f, (float)i);
                glVertex2f((float)fFBOSizeW, (float)i + dynamicOffset);
            }
        glEnd();

        // =========================================================================
        // FIXED CLEANUP LAYER: ISOLATE THE TRUE BOTTOM TASKBAR VIEWPORT GRID
        // =========================================================================
        // Drop back to the standard hardware screen framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Restrict rendering explicitly inside our 140px bar panel strip container
        glViewport(0, 0, fWidth, 140); 
        
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        
        // Map ONLY the absolute bottom 140-pixel strip coordinates of the screen space
        float taskbarTopCoordinate    = static_cast<float>(fHeight) - 140.0f;
        float taskbarBottomCoordinate = static_cast<float>(fHeight);
        
        gluOrtho2D(0.0, static_cast<float>(fWidth), taskbarBottomCoordinate, taskbarTopCoordinate);
        
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    // Framebuffer Object tracking metrics
    GLuint fWindowFBO = 0;
    GLuint fWindowRenderTexture = 0;
    
    // Virtual resolution of our inner window canvas panel (e.g., 512x512)
    //int fFBOSizeW = 512;
    //int fFBOSizeH = 512;
    
    int fFBOSizeW;
    int fFBOSizeH;

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

    HaikuPoint fWindowPos;
    float fWindowWidth, fWindowHeight;
    float fTabWidth, fTabHeight;
    
    bool fIsDragging;
    float fDragOffsetX, fDragOffsetY;

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
    
    HaikuTexture fMenuTextApps;
    int          fMenuTextAppsW = 0, fMenuTextAppsH = 0;

    HaikuTexture fMenuTextPrefs;
    int          fMenuTextPrefsW = 0, fMenuTextPrefsH = 0;

    HaikuTexture fMenuTextShutdown;
    int          fMenuTextShutdownW = 0, fMenuTextShutdownH = 0;
    
    std::string fCurrentPath;
    std::vector<BrowserFileItem> fBrowserItems;
    
    // Tracking parameters for mouse interaction
    uint32 fLastClickTime = 0;
    int fLastClickedIndex = -1;  
    
    float fScrollOffset = 0.0f;
    float fMaxScrollOffset = 0.0f;

    // Dynamic Navigation Path Text Layer
    HaikuTexture fPathTextTexture;
    int          fPathTextW = 0, fPathTextH = 0;

    // Bottom Status Bar Count Text Layer
    HaikuTexture fStatusTextTexture;
    int          fStatusTextW = 0, fStatusTextH = 0;
    
   // Window Functional States
    bool fWindowIsOpen = true;
    bool fWindowIsMinimized = false;

    // Dedicated Navigator Dock Icon Sizing Sockets
    HaikuTexture fNavigatorIcon;
    HaikuTexture fNavigatorText;
    int          fNavigatorTextW = 0, fNavigatorTextH = 0;

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

    // Set target rendering pipeline attributes to clean legacy OpenGL standards
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

    // =========================================================================
    // WINDOW GEOMETRY HEIGHT REFINEMENT (140px TALL SLIM BAR)
    // =========================================================================
    int dockPanelW = screenWidth;
    int dockPanelH = 160; // Safely holds magnifying icons and tooltips!
    int dockPanelX = 0;
    int dockPanelY = screenHeight - dockPanelH;

    // Instantiate a localized borderless panel strip resting at the screen bottom
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
    
    
    // =========================================================================
    // NATIVE SYSTEM WORKSPACE ROUTING: LOCKED DESKTOP LAYER SHIELD (FIXED)
    // =========================================================================
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version); // Initializing framework version packet handshake payload

    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        // Extract the first data address block directly from the info payload memory.
        // This completely skips anonymous union identifier name differences across SDL2 branches.
        void* rawWindowPointer = nullptr;
        std::memcpy(&rawWindowPointer, &wmInfo.info, sizeof(void*));
        
        BWindow* haikuWindow = reinterpret_cast<BWindow*>(rawWindowPointer);
        
        if (haikuWindow != nullptr) {
            std::cout << "[Native System] Intercepted raw BWindow instance layer pointer safely." << std::endl;
            
            // Gain safe synchronous window thread locking authorization
            if (haikuWindow->Lock()) {
                // Force the app_server compositor to pin this canvas layer beneath your workspace apps.
                // Value 2 maps explicitly to the background look/feel behavior type in Haiku
                haikuWindow->SetFeel(static_cast<window_feel>(2)); 
                
                // COMBINE CRITICAL MODIFIER FLAGS (FIXES ACTIVE LOSS OF FOCUS BUG)
                // - B_AVOID_FOCUS (0x00020000): Tells the window compositor to deny activation requests entirely.
                // - B_NOT_ANCHORED_ON_ACTIVATE (0x00010000): Ensures workspace shifting leaves this canvas pristine.
                uint32 originalFlags = haikuWindow->Flags();
                uint32 desktopShellFlags = 0x00020000 | 0x00010000;
                
                haikuWindow->SetFlags(originalFlags | desktopShellFlags);
                
                haikuWindow->Unlock();
                std::cout << "[Native System] Desktop constraints applied! Focus tracking secured." << std::endl;
            }
        }
    } else {
        std::cerr << "[Native System Failure] Could not route window handle context: " << SDL_GetError() << std::endl;
    }
    // =========================================================================



    
    
    

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    



    // =========================================================================
    // LOAD MODERN FBO GRAPHICS DRIVER COMMAND SYMBOLS FOR THE NVIDIA CARD
    // =========================================================================
    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");

    // Quick verification safety check
    if (!glGenFramebuffers || !glBindFramebuffer || !glFramebufferTexture2D || !glCheckFramebufferStatus) {
        std::cerr << "[GL ERROR] The loaded graphics driver doesn't support FBOs!" << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    // =========================================================================

    SDL_GL_SetSwapInterval(1);


    // =========================================================================
    // CRITICAL STEPS: THE 2D COORDINATE TRANSLATION MAPPER CONVERSION
    // =========================================================================
    // Configure viewport rendering constraints to our updated 140px height strip
    glViewport(0, 0, screenWidth, 160);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // --- PERFECT WALLPAPER RE-STITCH ALIGNMENT MATH ---
    // We clip the matrix boundary to map ONLY the bottom strip of the screen.
    // This allows your wallpaper texture to match the system desktop perfectly!
    float panelTopY    = static_cast<float>(screenHeight) - 160.0f;
    float panelBottomY = static_cast<float>(screenHeight);
    
    gluOrtho2D(0.0, static_cast<float>(screenWidth), panelBottomY, panelTopY);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Instantiate our unified Drawing Workspace Engine Controller Class
    HaikuGlDesktopEngine desktopEngine(screenWidth, screenHeight);


    bool appExecuting = true;
    SDL_Event incomingEventPackage;
    
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
