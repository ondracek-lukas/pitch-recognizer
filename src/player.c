// Pitch Recognizer   Copyright (C) 2018        Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3, see README file
// MusA               Copyright (C) 2016--2018  Lukáš Ondráček <ondracek.lukas@gmail.com>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "player.h"
#include "playerFile.h"
#include "playerDevice.h"
#include "messages.h"
#include "taskManager.h"
#include "util.h"
#include "mem.h"
#include "resources.gen.h"

enum playerSourceType playerSourceType = PLAYER_SOURCE_NONE;
char *playerSource = NULL;
int playerPos = 0;
double playerPosSec = 0;
double playerDuration = 0;
bool playerPlaying = false;

static __attribute__((constructor)) void init() {
	sbReset(&playerBuffer, 0, 0, 0);
	playerPos=0;
	playerSampleRate=1;
}

static void close();
void playerOpenDeviceDefault() {
	playerOpenDevice(44100 * 8);
}
void playerOpenDevice(double sampleRate) {
	close();
	if (!playerDeviceInputOpen(sampleRate)) {
		return;
	}

	char *source = memMalloc(sizeof(char) * 13);
	strcpy(source, "input device");
	playerSource = source;
	playerSourceType = PLAYER_SOURCE_DEVICE;

	msgSend_newSource();
	playerPlay();
}

void playerOpen(char *filename) {
	close();
	filename = utilExpandPath(filename);

	if (!playerFileOpen(filename)) {
		return;
	}

	if (!playerDeviceOutputOpen()) {
		playerFileClose();
		return;
	}

	char *source = memMalloc(sizeof(char) * strlen(filename));
	strcpy(source, filename);
	playerSource = source;
	playerSourceType = PLAYER_SOURCE_FILE;

	msgSend_newSource();
	playerPlay();
}

static void close() {
	enum playerSourceType type = playerSourceType;
	playerSourceType = PLAYER_SOURCE_NONE;
	if (playerSource) {
		char *source = playerSource;
		playerSource = NULL;
		free(source);
	}
	switch (type) {
		case PLAYER_SOURCE_FILE:
			playerFileClose();
			playerDeviceClose();
			break;
		case PLAYER_SOURCE_DEVICE:
			playerDeviceClose();
			break;
	}
	int playerPos = 0;
	double playerPosSec = 0;
	double playerDuration = 0;
	sbReset(&playerBuffer, 0, 0, 0);
}


extern void playerPlay() {
	switch (playerSourceType) {
		case PLAYER_SOURCE_FILE:
		case PLAYER_SOURCE_DEVICE:
			playerDeviceStart();
			break;
	}
}

extern void playerPause() {
	switch (playerSourceType) {
		case PLAYER_SOURCE_FILE:
		case PLAYER_SOURCE_DEVICE:
			playerDeviceStop();
			break;
	}
}

extern void playerSeekAbs(double posSec) {
	switch (playerSourceType) {
		case PLAYER_SOURCE_FILE:
			{
				bool playing = playerPlaying;
				if (playing) {
					playerDeviceStop();
				}
				if (posSec > playerDuration) {
					posSec = playerDuration;
				}
				if (posSec < 0) posSec = 0;
				int pos = posSec * playerSampleRate;
				playerPos = pos;
				playerPosSec = posSec;
				if (playing) {
					playerDeviceStart();
				}
				break;
			}
		default:
			msgSend_printErr("Cannot seek.");
	}
}
extern void playerSeekRel(double sec) {
	playerSeekAbs(playerPosSec + sec);
}
