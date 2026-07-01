#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>

// Native Haiku OS headers
#include <Bitmap.h>
#include <IconUtils.h>
#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <NodeInfo.h>

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


#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glu.h>
#include <OS.h>


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
    std::string title;       // Window identifier (e.g., "Navigator")
    HaikuTexture icon;       // Quick reference to its active launcher texture
    bool isMinimized;        // Tracks state to paint a highlighted alert overlay
    bool* openStateFlag;     // Points directly to fWindowIsOpen to sync active lifetime
    bool* minimizeStateFlag; // Points directly to fWindowIsMinimized to sync window state
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


// =========================================================================
// THE HAIKU DESKTOP DRAW ENGINE RENDERING INTERFACE CLASS
// =========================================================================
class HaikuGlDesktopEngine {
public:
    HaikuGlDesktopEngine(int width, int height) : fWidth(width), fHeight(height) {
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

        fHaikuMenuIcon = LoadIconFromNode("/boot/system/apps/AboutSystem", 128);
        fWallpaperTexture = LoadWallpaperViaTranslationKit("/boot/system/data/hdesktop/background/background.png");
		
		
        // =========================================================================
        // INITIALIZE SYSTEM MONITOR AND WINDOW STATE UTILITIES
        // =========================================================================
        fWindowIsOpen = true;
        fWindowIsMinimized = false;
        fLastCpuPulseTime = SDL_GetTicks();

        fNavigatorIcon = LoadIconFromNode("/boot/system/preferences/Tracker", 128);
        fNavigatorText = RenderTextToTexture("Navigator", &fNavigatorTextW, &fNavigatorTextH);

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
        
        // =========================================================================
        // INITIALIZE LIVE FILESYSTEM TRACKER BROWSER WINDOW
        // =========================================================================
        RefreshWindowDirectory("/boot/home");

        
        // =========================================================================
        // INITIALIZE CUSTOM START MENU TYPOGRAPHY LABELS
        // =========================================================================
        fMenuTextApps     = RenderTextToTexture("Applications", &fMenuTextAppsW, &fMenuTextAppsH);
        fMenuTextPrefs    = RenderTextToTexture("Preferences",  &fMenuTextPrefsW,  &fMenuTextPrefsH);
        fMenuTextShutdown = RenderTextToTexture("Shutdown...",   &fMenuTextShutdownW, &fMenuTextShutdownH);


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

    
       void HandleMouseClick(int x, int y) {
        // =========================================================================
        // 1. WINDOW BUTTON CORNER CONTROLS HIT DETECTION INTERCEPTS
        // =========================================================================
        if (fWindowIsOpen && !fWindowIsMinimized) {
            HaikuRect winWin = { fWindowPos.x, fWindowPos.y, fWindowPos.x + fWindowWidth, fWindowPos.y + fWindowHeight };
            float titleBarHeight = 16.0f;
            HaikuRect topExtenderBar = { winWin.left, winWin.top - titleBarHeight, winWin.right, winWin.top };
            float barCenterY = topExtenderBar.top + (topExtenderBar.bottom - topExtenderBar.top) / 2.0f;

            float btnW = 14.0f;
            float btnH = 12.0f;

            HaikuRect closeBoxBtn = {
                winWin.right - 6.0f - btnW,
                barCenterY - (btnH / 2.0f),
                winWin.right - 6.0f,
                barCenterY + (btnH / 2.0f)
            };

            HaikuRect maxToggleBtn = {
                closeBoxBtn.left - 4.0f - btnW,
                closeBoxBtn.top,
                closeBoxBtn.left - 4.0f,
                closeBoxBtn.bottom
            };

            HaikuRect zoomToggleBtn = {
                maxToggleBtn.left - 4.0f - btnW,
                maxToggleBtn.top,
                maxToggleBtn.left - 4.0f,
                maxToggleBtn.bottom
            };

            HaikuRect resizeGrip = { winWin.right - 14.0f, winWin.bottom - 14.0f, winWin.right, winWin.bottom };

            if (closeBoxBtn.Contains(x, y)) {
                std::cout << "[Window Control] Intercepted Close Box Event Packet." << std::endl;
                fWindowIsOpen = false;
                return;
            }

            if (maxToggleBtn.Contains(x, y)) {
                std::cout << "[Window Control] Toggle Maximization Event Packet." << std::endl;
                if (!fWindowIsMaximized) {
                    fSavedWindowX = fWindowPos.x; fSavedWindowY = fWindowPos.y;
                    fSavedWindowW = fWindowWidth; fSavedWindowH = fWindowHeight;
                    fWindowPos.x = 20.0f; fWindowPos.y = 60.0f;
                    fWindowWidth = static_cast<float>(fWidth) - 40.0f;
                    fWindowHeight = static_cast<float>(fHeight) - 220.0f;
                    fWindowIsMaximized = true;
                } else {
                    fWindowPos.x = fSavedWindowX; fWindowPos.y = fSavedWindowY;
                    fWindowWidth = fSavedWindowW; fWindowHeight = fSavedWindowH;
                    fWindowIsMaximized = false;
                }
                float totalListHeight = fBrowserItems.size() * 30.0f;
                float canvasHeight = fWindowHeight - 4.0f - 20.0f;
                fMaxScrollOffset = totalListHeight - canvasHeight;
                if (fMaxScrollOffset < 0.0f) fMaxScrollOffset = 0.0f;
                return;
            }

            if (zoomToggleBtn.Contains(x, y)) {
                std::cout << "[Window Control] Intercepted Minimize Box Event Packet." << std::endl;
                fWindowIsMinimized = true;
                return;
            }

            if (resizeGrip.Contains(x, y)) {
                std::cout << "[Window Control] Grabbed Purple Resize Handle." << std::endl;
                fIsResizing = true;
                fResizeStartX = x; fResizeStartY = y;
                fResizeStartWidth = fWindowWidth; fResizeStartHeight = fWindowHeight;
                return;
            }

            // =========================================================================
            // 2. INTERACTIVE FILESYSTEM LIST DOUBLE-CLICK CHECK
            // =========================================================================
            for (size_t j = 0; j < fBrowserItems.size(); ++j) {
                if (fBrowserItems[j].clickBounds.Contains(x, y)) {
                    uint32 currentTicks = SDL_GetTicks();
                    if (fLastClickedIndex == (int)j && (currentTicks - fLastClickTime) < 400) {
                        if (fBrowserItems[j].isFolder) {
                            std::cout << "[Tracker window] Moving to directory node: " << fBrowserItems[j].fullPath << std::endl;
                            RefreshWindowDirectory(fBrowserItems[j].fullPath);
                        } else {
                            std::cout << "[Tracker window] Executing file natively: " << fBrowserItems[j].name << std::endl;
                            BEntry entry(fBrowserItems[j].fullPath.c_str());
                            entry_ref ref;
                            if (entry.GetRef(&ref) == B_OK) {
                                be_roster->Launch(&ref);
                            }
                        }
                        fLastClickedIndex = -1;
                    } else {
                        fLastClickedIndex = (int)j;
                        fLastClickTime = currentTicks;
                    }
                    return;
                }
            }
        }

        // =========================================================================
        // 3. PIXEL-PERFECT TASKBAR-ENABLED DOCK CLICK EVALUATOR
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
        float totalDockWidth = 0.0f;

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
            totalDockWidth += finalSize + padding;
        }
        totalDockWidth -= padding;

        float clockSectionPadding = 24.0f;
        float cpuGraphWidth = 60.0f;
        float separatorGapPadding = 16.0f;
        
        float finalPlateWidth = totalDockWidth + clockSectionPadding + fClockWidth + clockSectionPadding + cpuGraphWidth;
        if (activeWindowsCount > 0) finalPlateWidth += separatorGapPadding;

        HaikuRect dockPlate;
        dockPlate.left = (fWidth / 2.0f) - (finalPlateWidth / 2.0f) - 20.0f;
        dockPlate.bottom = fHeight - 15.0f;

        // =========================================================================
        // DOCK RUNNING CLICK ROUTER AT METRIC MARGINS
        // =========================================================================
        float currentX = dockPlate.left + 20.0f;
        size_t evaluationSlotIdx = 0;

        // STEP A: EVALUATE BASELINE SYSTEM LAUNCHERS (MENU LEAF + FILE SHORTCUTS)
        for (size_t i = 0; i < baselineLaunchersCount; ++i) {
            float size = dynamicWidths[evaluationSlotIdx];
            HaikuRect realIconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };

            if (realIconBounds.Contains(x, y)) {
                if (i == 0) {
                    fShowMainMenu = !fShowMainMenu;
                    if (fShowMainMenu) {
                        fMainMenuBounds = { realIconBounds.left, realIconBounds.top - 220.0f, realIconBounds.left + 180.0f, realIconBounds.top - 4.0f };
                    }
                } else {
                    // --- FIXED INTERCEPT CHANNELS ---
                    // Correctly map your click indices straight back to the desktop folder item array bounds!
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

            if (realIconBounds.Contains(x, y)) {
                fShowMainMenu = false;
                std::cout << "[Taskbar Click] Intercepted Request for: " << activeTaskWin.title << std::endl;
                
                // Toggle minimize and active focus presence on task click!
                if (*activeTaskWin.minimizeStateFlag == false) {
            		*activeTaskWin.minimizeStateFlag = true; 
            		// Collapse window down
            		} else {
            			*activeTaskWin.openStateFlag = true;
            			*activeTaskWin.minimizeStateFlag = false; // Restore window back to active view
            			}
            		return;
            	}
            currentX += size + padding;
           evaluationSlotIdx++;
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
        UpdateLiveClockTexture();
        UpdateGlobalCpuLoadTracker(); // Pull processor ticks and update our 40-cell buffer array

        glClearColor(fBgColorR, fBgColorG, fBgColorB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // =========================================================================
        // 2. FULLSCREEN WALLPAPER DRAW PASS
        // =========================================================================
        if (fWallpaperTexture.id != 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, fWallpaperTexture.id);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // Pure white mask filter

            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(static_cast<float>(fWidth), 0.0f);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(static_cast<float>(fWidth), static_cast<float>(fHeight));
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, static_cast<float>(fHeight));
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
        
        // Append extra horizontal spacing only if there is an active running application window open!
        float finalPlateWidth = totalDockWidth + clockSectionPadding + fClockWidth + clockSectionPadding + cpuGraphWidth;
        if (activeWindowsCount > 0) {
            finalPlateWidth += separatorGapPadding;
        }

        float dockMarginBottom = 15.0f;
        HaikuRect dockPlate;
        dockPlate.left = (fWidth / 2.0f) - (finalPlateWidth / 2.0f) - 20.0f;
        dockPlate.right = (fWidth / 2.0f) + (finalPlateWidth / 2.0f) + 20.0f;
        dockPlate.bottom = fHeight - dockMarginBottom;
        dockPlate.top = dockPlate.bottom - maxDockHeight - 20.0f;

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
            if (*activeTaskWin.openStateFlag == false) continue; // Skip rendering completely if the window was closed!

            float size = dynamicWidths[renderingSlotIdx];
            float scale = dynamicScales[renderingSlotIdx];
            HaikuRect iconBounds = { currentX, dockPlate.bottom - 10.0f - size, currentX + size, dockPlate.bottom - 10.0f };

            // A. Draw active task window application vector icon thumbnail
            if (activeTaskWin.icon.id != 0) {
                glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, activeTaskWin.icon.id);
                
                // If the app window is currently minimized, draw it slightly translucent to signify focus sleep!
                if (*activeTaskWin.minimizeStateFlag == true) {
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
            if (*activeTaskWin.minimizeStateFlag == false) {
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

        // =========================================================================
        // 11. DRAW CUSTOM INTERNAL SEMI-TRANSPARENT MAIN MENU OVERLAY WITH TEXT
        // =========================================================================
        if (fShowMainMenu) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.847f, 0.847f, 0.847f, 0.85f); 
            glBegin(GL_QUADS);
                glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.top);   glVertex2f(fMainMenuBounds.right, fMainMenuBounds.top);
                glVertex2f(fMainMenuBounds.right, fMainMenuBounds.bottom); glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.bottom);
            glEnd();

            glColor4f(0.196f, 0.196f, 0.196f, 0.9f); glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
                glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.top);   glVertex2f(fMainMenuBounds.right, fMainMenuBounds.top);
                glVertex2f(fMainMenuBounds.right, fMainMenuBounds.bottom); glVertex2f(fMainMenuBounds.left,  fMainMenuBounds.bottom);
            glEnd();

            glColor4f(1.0f, 1.0f, 1.0f, 0.6f);
            glBegin(GL_LINE_STRIP);
                glVertex2f(fMainMenuBounds.left + 1.0f,  fMainMenuBounds.bottom - 1.0f);
                glVertex2f(fMainMenuBounds.left + 1.0f,  fMainMenuBounds.top + 1.0f);
                glVertex2f(fMainMenuBounds.right - 1.0f, fMainMenuBounds.top + 1.0f);
            glEnd();

            glColor4f(0.6f, 0.6f, 0.6f, 0.4f); float stepY = 30.0f;
            glBegin(GL_LINES);
                for (float y = fMainMenuBounds.top + stepY; y < fMainMenuBounds.bottom; y += stepY) {
                    glVertex2f(fMainMenuBounds.left + 4.0f,  y); glVertex2f(fMainMenuBounds.right - 4.0f, y);
                }
            glEnd();

            glEnable(GL_TEXTURE_2D);
            glColor4f(0.0f, 0.0f, 0.0f, 1.0f); 
            float textIndentX = fMainMenuBounds.left + 12.0f;

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

        // Release the background canvas tracking back to monitor viewport coordinates
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fWidth, fHeight);
        
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0.0, fWidth, fHeight, 0.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    // Framebuffer Object tracking metrics
    GLuint fWindowFBO = 0;
    GLuint fWindowRenderTexture = 0;
    
    // Virtual resolution of our inner window canvas panel (e.g., 512x512)
    int fFBOSizeW = 512;
    int fFBOSizeH = 512;

    // Keep all your previous layout structures (fDesktopItems, fShowMainMenu, etc.) intact!

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


    // Helper function that talks to Haiku NodeInfo to read any specific file's icon data
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

            // COLOR CORRECTION PASS: Swapped internal format parameter to GL_BGRA 
            // This fixes the pink folder tabs and gives you native golden-yellow tracker icons!
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

    // Fetch the target physical monitor screen bounds resolution automatically
    SDL_DisplayMode currentMode;
    if (SDL_GetCurrentDisplayMode(0, &currentMode) != 0) {
        std::cerr << "Display tracking lookup failure: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    int screenWidth  = currentMode.w;
    int screenHeight = currentMode.h;

    // Construct full-screen context layout boundaries directly
    SDL_Window* window = SDL_CreateWindow(
        "Haiku GL Desktop Sandbox Workspace Engine Container",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screenWidth, screenHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN
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
    glViewport(0, 0, screenWidth, screenHeight);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // Project exact 1:1 pixel coordinate landscape mapping match to screen boundaries
    // Top-Left corner registers explicitly at (0.0, 0.0) mirroring BeAPI expectations!
    gluOrtho2D(0.0, screenWidth, screenHeight, 0.0);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Instantiate our unified Drawing Workspace Engine Controller Class
    HaikuGlDesktopEngine desktopEngine(screenWidth, screenHeight);

    bool appExecuting = true;
    SDL_Event incomingEventPackage;
    
   	{
    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/hdesktop/refs/heads/main/VERSION";
    const char* localVersion = "v1.0.3"; 

    char updateCmd[1024];
    snprintf(updateCmd, sizeof(updateCmd),
        "(REMOTE_V=$(curl -sL \"%s\" | tr -d '\\r\\n'); "
        "if [ ! -z \"$REMOTE_V\" ] && [ \"$REMOTE_V\" != \"%s\" ]; then "
        "notify --title \"Update Available\" --group \"hDesktop\" "
        "\"A newer version of hDesktop is available! ($REMOTE_V)\"; fi) &",
        targetUrl, localVersion);	
    system(updateCmd);
   }
    

	while (appExecuting) {
	    while (SDL_PollEvent(&incomingEventPackage)) {
	        if (incomingEventPackage.type == SDL_QUIT) {
	            appExecuting = false;
	        }
	        
	        else if (incomingEventPackage.type == SDL_KEYDOWN) {
	            if (incomingEventPackage.key.keysym.sym == SDLK_ESCAPE) {
	                appExecuting = false;
	            }
	        }
	        // Catch BOTH mouse clicking and sliding actions simultaneously!
	        else if (incomingEventPackage.type == SDL_MOUSEMOTION || 
	                 incomingEventPackage.type == SDL_MOUSEBUTTONDOWN ||
	                 incomingEventPackage.type == SDL_MOUSEBUTTONUP) {
	            int mouseX, mouseY;
	            Uint32 buttons = SDL_GetMouseState(&mouseX, &mouseY);
	            desktopEngine.HandleMouseInput(mouseX, mouseY, buttons);

	            // =========================================================================
	            // ADDED: TRACK MOUSE PRESSES TO TRIGGER NATIVE HAIKU APPS LAUNCHES
	            // =========================================================================
	            if (incomingEventPackage.type == SDL_MOUSEBUTTONDOWN && 
	                incomingEventPackage.button.button == SDL_BUTTON_LEFT) {
	                desktopEngine.HandleMouseClick(mouseX, mouseY);
	            }
	        }
	        	        
	         else if (incomingEventPackage.type == SDL_MOUSEWHEEL) {
                desktopEngine.HandleMouseWheel(incomingEventPackage.wheel.y);
            }        
	        
	    }
	
	    desktopEngine.RenderFrame();
	    SDL_GL_SwapWindow(window);
	}



    // Explicit Context Resource Destruction sequence
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
