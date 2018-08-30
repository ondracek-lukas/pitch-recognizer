// Pitch Recognizer   Copyright (C) 2018        Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3, see README file
// MusA               Copyright (C) 2016--2018  Lukáš Ondráček <ondracek.lukas@gmail.com>

#ifndef PLAYER_FILE_H
#define PLAYER_FILE_H

#include <stdbool.h>

extern struct taskInfo playerFileTask;
extern struct taskInfo playerFileThreadTask;
extern bool playerFileOpen(char *filename);
extern void playerFileClose();
extern bool playerDataOpen(char *filename, const char *data, size_t size);
extern void playerDataClose();

#endif
