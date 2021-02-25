TEMPLATE	= app
HEADERS		= ../include/controlcenter.h
FORMS		= controlcenter.ui
SOURCES		= controlcenter.cpp main.cpp fx2_download.cpp fx3_download.cpp streamer.cpp
LIBS		+= -L../lib -lcyusb -lusb-1.0
QT		+= network
QT		+= widgets 
TARGET		= ../bin/cyusb_linux
