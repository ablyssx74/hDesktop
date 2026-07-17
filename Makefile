# Compiler definitions
CXX = g++
CXXFLAGS = -Wall -O3 -I/boot/system/develop/headers/private/app -I/boot/system/develop/headers/private/tracker -I./tracker


# Target binary definitions
GUI_TARGET = hdesktop
VERSION = 1.0.12
PACKAGE_DIR := build/package

# Shared target architectures
UNAME_M := $(shell uname -p)
ifeq ($(UNAME_M), x86)
    CXX = g++-x86 
    ARCH = x86_gcc2
    INCLUDE = -L/boot/system/lib/x86 
else ifeq ($(UNAME_M), x86_64)
    CXX = g++
    ARCH = x86_64
    INCLUDE = -L/boot/system/lib
endif

# =========================================================================
# FIXED SOURCE MAPPING PARAMETERS (Merged the new tray component target)
# =========================================================================
GUI_SRCS = hdesktop.cpp 
GUI_OBJS = $(GUI_SRCS:.cpp=.o)
GUI_RSRCS = hdesktop.rsrc


# Shared linking assets
LIBS = -L./lib -lbe -ltracker -ltranslation -lmedia -lSDL2 -lGL -lGLU -llocalestub


# OPTIMIZED: Added garbage collection linking flags and symbol stripping (-s)
LDFLAGS = $(INCLUDE) -Wl,--gc-sections -s

# Master target execution rule
all: $(GUI_TARGET) 

# Link the graphical desktop client binary
$(GUI_TARGET): $(GUI_OBJS) $(GUI_RSRCS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(GUI_TARGET) $(GUI_OBJS) $(LIBS) 
	xres -o $(GUI_TARGET) $(GUI_RSRCS)
	mimeset -f $(GUI_TARGET)



# Compile visual layout script components
%.rsrc: %.rdef
	rc -o $@ $<

# General object file compilation hooks
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Deep system cleaning target
clean:
	rm -f *.o *.rsrc $(GUI_TARGET) 
	rm -f $(NAME) *.hpkg
	rm -rf build

.PHONY: all clean




release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(GUI_TARGET)/$(GUI_TARGET)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(GUI_TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	#mkdir -p $(PACKAGE_DIR)/data/$(GUI_TARGET)/background/
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(GUI_TARGET) $(PACKAGE_DIR)/apps/$(GUI_TARGET)
	#cp background.png $(PACKAGE_DIR)/data/$(GUI_TARGET)/background/
	ln -s /boot/system/apps/$(GUI_TARGET) $(PACKAGE_DIR)/bin/$(GUI_TARGET)
	ln -s /boot/system/apps/$(GUI_TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(GUI_TARGET)
	package create -C $(PACKAGE_DIR) $(GUI_TARGET)-$(VERSION)-1-$(ARCH).hpkg	



