#QT += core
#QT -= gui

QT += gui
QT += widgets

CONFIG += c++11

RC_FILE = CoverFS.rc

TARGET = CoverFS
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp
