// Pitch Recognizer   Copyright (C) 2018        Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3, see README file
// MusA               Copyright (C) 2016--2018  Lukáš Ondráček <ondracek.lukas@gmail.com>

#ifndef DRAWER_H
#define DRAWER_H

extern struct taskInfo drawerMainTask;
extern struct taskInfo drawerConsoleTask;

extern double drawerVisibleBegin;
extern double drawerVisibleEnd;

extern void drawerInit();
extern void drawerReset();

#endif
