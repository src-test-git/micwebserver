QT       += core gui network widgets multimedia
CONFIG += c++11
LIBS += -lavformat -lavcodec -lavdevice -lavfilter -lavutil -lfdk-aac
QMAKE_CXXFLAGS += -Wno-deprecated-copy
QMAKE_CXXFLAGS += -Wno-deprecated-declarations

SOURCES += \
	main.cpp \
	mainwindow.cpp

HEADERS += \
	mainwindow.h

FORMS += \
	mainwindow.ui

