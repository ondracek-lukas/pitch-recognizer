// Pitch Recognizer   Copyright (C) 2018        Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3, see README file
// MusA               Copyright (C) 2016--2018  Lukáš Ondráček <ondracek.lukas@gmail.com>

#ifndef PLAYER_DEVICE_H
#define PLAYER_DEVICE_H

#include <stdbool.h>

bool playerDeviceOutputOpen();
bool playerDeviceInputOpen(double sampleRate);
void playerDeviceClose();
void playerDeviceStart();
void playerDeviceStop();

#endif
