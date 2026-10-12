# Compiler definitions
CXX = g++
CXXFLAGS = -Wall -O3 -I/boot/system/develop/headers/private/app -I/boot/system/develop/headers/private/tracker -I./tracker -I/boot/system/develop/headers/private/interface


# Target binary definitions
GUI_TARGET = hdesktop
# The one place a release changes is the VERSION file (v1.0.63): the program, the package and the resource all take it from there.
VERSION := $(shell tr -d 'v \r\n' < VERSION)
V_MAJOR := $(word 1,$(subst ., ,$(VERSION)))
V_MINOR := $(word 2,$(subst ., ,$(VERSION)))
V_MIDDLE := $(word 3,$(subst ., ,$(VERSION)))
PACKAGE_DIR := build/package
REVISION = 1

# Shared target architectures
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M), BePC)
    CXX = g++-x86 
    ARCH = x86_gcc2
    INCLUDE = -L/boot/system/lib/x86 
    is32bit = _x86
    DEFINES += -DIS_HAIKU_32BIT
else
    CXX = g++
    ARCH = x86_64
    INCLUDE = -L/boot/system/lib
endif

DEFINES := $(DEFINES)
DEFINES += -DAPP_LOCAL_VERSION=\"v$(VERSION)\"

# =========================================================================
# FIXED SOURCE MAPPING PARAMETERS (Merged the new tray component target)
# =========================================================================
GUI_SRCS = hdesktop.cpp 
GUI_OBJS = $(GUI_SRCS:.cpp=.o)
GUI_RSRCS = hdesktop.rsrc


# Shared linking assets
# -lgame: BDirectWindow's own symbols live in Haiku's separate Game Kit lib,
# not libbe itself -- needed for SetupDirectCaptureIfNeeded()'s DirectCaptureWindow.
LIBS = -L./lib -lbe -lgame -ltracker -ltranslation -lmedia -lSDL2 -lGL -lGLU -llocalestub -lcurl


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
# (the version fields of the resource are filled in from the VERSION file)
%.rsrc: %.rdef VERSION
	sed -e 's/@V_MAJOR@/$(V_MAJOR)/' -e 's/@V_MINOR@/$(V_MINOR)/' -e 's/@V_MIDDLE@/$(V_MIDDLE)/' -e 's/@VERSION@/$(VERSION)/' $< > $*.version.rdef
	rc -o $@ $*.version.rdef
	rm -f $*.version.rdef

# General object file compilation hooks (the version is compiled in: a new VERSION means a new build)
$(GUI_OBJS): VERSION
%.o: %.cpp
	$(CXX) $(DEFINES) $(CXXFLAGS) -c $< -o $@

# Deep system cleaning target
clean:
	rm -f *.o *.rsrc *.version.rdef $(GUI_TARGET) 
	rm -f $(NAME) *.hpkg
	rm -rf build

.PHONY: all clean




release: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(GUI_TARGET)/$(GUI_TARGET)/g' -e 's/$$(REVISION)/$(REVISION)/g' -e 's/$$(is32bit)/$(is32bit)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(GUI_TARGET).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	cp $(GUI_TARGET) $(PACKAGE_DIR)/apps/$(GUI_TARGET)
	ln -s /boot/system/apps/$(GUI_TARGET) $(PACKAGE_DIR)/bin/$(GUI_TARGET)
	ln -s /boot/system/apps/$(GUI_TARGET) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(GUI_TARGET)
	package create -C $(PACKAGE_DIR) $(GUI_TARGET)-$(VERSION)-$(REVISION)-$(ARCH).hpkg	



