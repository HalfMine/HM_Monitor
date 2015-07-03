TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp \
    Profile.cpp


HEADERS += \
    Profile.h \
    dhnetsdk.h \
    dhassistant.h

unix: LIBS += -ldhnetsdk

unix: LIBS += -L /usr/lib64/mysql -lmysqlclient

unix: LIBS += -lavformat
