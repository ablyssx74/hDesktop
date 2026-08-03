name			$(GUI_TARGET)
version			$(VERSION)-1
architecture	$(ARCH)
summary 		"hdesktop"
description 	"hdesktop - SDL2 OpenGL Hyrbid Desktop Manager"
packager		"ablyss <hdesktop@epluribusunix.net>"
vendor			"epluribusunix.net Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(GUI_TARGET) = $(VERSION)-1
}
requires {
	haiku
	libsdl2$(is32bit)
	curl$(is32bit)
}	
urls {
	"https://github.com/ablyssx74/hdesktop"
}
