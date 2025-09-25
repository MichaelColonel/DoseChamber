#-------------------------------------------------
#
# Project created by QtCreator 2015-12-10T11:21:43
#
#-------------------------------------------------

QT += core gui

CONFIG += qt warn_on thread

!exists ($(ROOTSYS)/include/rootcint.pri) {
    message (The Rootcint.pri was not found)
}
exists ($(ROOTSYS)/include/rootcint.pri) {
    include ($(ROOTSYS)/include/rootcint.pri)
}

# With C++11 support
greaterThan( QT_MAJOR_VERSION, 4) {
    QT += widgets serialport
    CONFIG += c++11
} else {
    QMAKE_CXXFLAGS += -std=c++11
    CONFIG += serialport
}

TARGET = DoseChamber

TEMPLATE = app

SOURCES += main.cpp \
    mainwindow.cpp \
    canvas.cpp

HEADERS  += mainwindow.h \
    canvas.h

FORMS    += mainwindow.ui


unix {
}

win32 {
}

#RESOURCES += DoseChamber.qrc
#TRANSLATIONS += DoseChamber_ru.ts
