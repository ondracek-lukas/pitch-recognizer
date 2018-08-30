// Pitch Recognizer   Copyright (C) 2018        Lukáš Ondráček <ondracek.lukas@gmail.com>, use under GNU GPLv3, see README file

// Warning:
//   This file was written within two days to become ready to use
//   at the expense of its readability...

#include <string.h>
#include "player.h"
#include "taskManager.h"
#include "streamBuffer.h"
#include "fft.h"
#include <float.h>
#include <math.h>
#include "drawerScale.h"
#include <stdio.h>
#include "mem.h"
#include "messages.h"

#define STATUS_LEN 32
#define OUTPUT_LEN 1000
#define NEXT_PEAK_QUALITY_INCRESE 1.2
#define PEAKS_TO_EXAMINE 8

double shiftSec;
int blockSizeLog;
int blockSize;
int blockTailSize;
int blockRetSize;
int maxAllowedPeriod;
int minGapLen;

struct taskInfo prTask = TM_TASK_INITIALIZER(false, false); // active, serial
struct taskInfo prOutTask = TM_TASK_INITIALIZER(false, true);

int pos = 0;
int outputPos = 0;

enum {
	FREE,
	RESERVED,
	OUTPUT,
	DONE
} status[STATUS_LEN];
// char outputs[STATUS_LEN][OUTPUT_LEN];
struct outputT {
	char str[OUTPUT_LEN];
	double quality;
} outputs[STATUS_LEN];

struct memPerThread *bufferPerThread;

void toneNameCents(char *out, double freq) {
	double tone = dsFreqToTone(freq);
	int nearestTone = round(tone);
	char toneName[30];
	dsGetToneName(toneName, nearestTone);
	double cents = (tone-nearestTone) * 100;

	snprintf(out, 30, "%5s %s%s%.2f",
		toneName, (fabs(cents) < 10 ? " " : ""), (cents >= 0 ? "+" : ""), cents);
}

int findPrevPeak(float *buffer, int lastIndex, int direction, float threshold) {
	double lastPeakVal = buffer[lastIndex];
	int lastPeakIndex  = 0;

	for (int i = lastIndex; i >= 0; i--) {
		if (direction * lastPeakVal < direction * buffer[i]) {
			lastPeakVal = buffer[i];
			lastPeakIndex = i;
		}
		if ((direction * lastPeakVal >= direction * threshold) && (i <= lastIndex-(lastIndex-lastPeakIndex)*2-2)) break;
	}

	return lastPeakIndex;
}
int findPrevMaxPeak(float *buffer, int lastIndex) {
	int peakIndex = lastIndex;
	peakIndex     = findPrevPeak(buffer, peakIndex, -1, 1); // min
	peakIndex     = findPrevPeak(buffer, peakIndex,  1, 0); // max
	return peakIndex;
}
double calcPeriodQuality(float *buffer, double period) {
	double index = blockRetSize-1;
	// double sum = 0;
	// int cnt = 0;
	double max = 0;
	while (index > 0) {
		double value = 1 - buffer[(int)round(index)];
		if (max < value) max = value;
		// sum += 1 - buffer[(int)round(index)];
		// cnt++;
		index -= period;
	}
	// double quality = (1 - sum1/cnt1*2) * 100;
	double quality = (1 - max) * 100;
	if (quality < 0) quality = 0;
	return quality;
}

int fineTunePeakIndex(float *buffer, int peakIndex, int maxDist) {
	int minIndex = peakIndex - maxDist;
	if (minIndex < 0) minIndex = 0;
	int maxIndex = peakIndex + maxDist;
	if (maxIndex > blockRetSize-1) maxIndex = blockRetSize-1;
	float peakValue = buffer[peakIndex];
	for (int i = minIndex; i <= maxIndex; i++) {
		if (buffer[i] > peakValue) {
			peakValue = buffer[i];
			peakIndex = i;
		}
	}
	return peakIndex;
}
double fineTunePeriod(float *buffer, double period) {
	double origPeriod = period;
	double newPeriod = period;
	for (int mult = 1; ; mult++) {
		if (period < 1) { // XXX
			printf("LOW PERIOD: %10.3f, orig: %10.3f, mult: %d\n", period, origPeriod, mult);
			exit(42);
		}
		int index = blockRetSize-1 - mult*newPeriod;
		if (index < 0) break;
		index = fineTunePeakIndex(buffer, index, 10);
		newPeriod = (blockRetSize-1-index)/(double)mult;
		if (newPeriod > 2) {
			period = (period + 2*newPeriod) / 3;
		} else {
			newPeriod = period;
		}
	}
	return period;
}

void processBlock(int streamPos, struct outputT *output) {
	(*output).str[0] = '\0';
	float *buffer = memPerThreadGet(bufferPerThread);
	//float buffer[blockSize];
	//float *buffer = malloc(sizeof(float) * blockSize);
	if (!buffer) exit(43);
	if (!sbRead(&playerBuffer, streamPos + blockSize - blockTailSize, streamPos + blockSize, buffer)) {
		printf("Cannot read tail...\n");
	}
	for (int i = 0; i < blockTailSize/2; i++) {
		float tmp = buffer[i]; buffer[i] = buffer[blockTailSize - i - 1]; buffer[blockTailSize - i - 1] = tmp;
	}
	struct fftFilterContext *ctx = fftCreateFilterContext(buffer, blockTailSize, blockSizeLog);
	if (!sbRead(&playerBuffer, streamPos, streamPos + blockSize, buffer)) {
		printf("Cannot read block...\n");
	}
	fftFilter(buffer, ctx);
	fftDestroyFilterContext(ctx);
	

	double maxValue = -DBL_MAX;
	int maxIndex = 0;
	double minValue = DBL_MAX;
	double avgValue = 0;
	double lastValue = buffer[blockSize-blockTailSize];
	for (int i = 0; i < blockRetSize; i++) {
		if (maxValue < buffer[i]) {
			maxValue = buffer[i];
			maxIndex = i;
		}
		if (minValue > buffer[i]) {
			minValue = buffer[i];
		}
		avgValue += buffer[i];
	}
	if (maxValue == minValue) {
		snprintf((*output).str, OUTPUT_LEN, "silence");
		(*output).quality=0;
		//free(buffer);
		return;
	}
	for (int i = 0; i < blockRetSize; i++) {
		buffer[i] = (buffer[i]-minValue)/(maxValue-minValue);
		//printf("%10.3f ", buffer[i]);
		//if (i%10 == 0) printf("\n");
	}
	avgValue /= blockSize - blockTailSize + 1;

	int peakIndex = blockRetSize - 1; // max

	double firstFreq, bestFreq, firstQuality, bestQuality, firstFreqFT, firstQualityFT, bestQualityFT=-DBL_MAX/2, bestFreqFT;
	int bestQualityIndex=-1;
	for (int i = 0; i < PEAKS_TO_EXAMINE; i++) {
		peakIndex     = findPrevMaxPeak(buffer, peakIndex);

		int period = blockRetSize - 1 - peakIndex;
		if (period > maxAllowedPeriod) break;
		double periodFT = fineTunePeriod(buffer, period);
		double freq = playerSampleRate / period;
		double freqFT = playerSampleRate / periodFT;
		double quality = calcPeriodQuality(buffer, period);
		double qualityFT = calcPeriodQuality(buffer, periodFT);

		if (i == 0) {
			firstFreq = freq;
			firstQuality = quality;
			firstFreqFT = freqFT;
			firstQualityFT = qualityFT;
		}
		if (bestQualityFT * NEXT_PEAK_QUALITY_INCRESE < qualityFT) {
			bestFreq = freq;
			bestQuality = quality;
			bestFreqFT = freqFT;
			bestQualityFT = qualityFT;
			bestQualityIndex = i;
		}
	}
	if (bestQualityIndex == -1) {
		snprintf((*output).str, OUTPUT_LEN, "no tone found");
		(*output).quality=0;
		return;
	}

	char toneName[30];
	toneNameCents(toneName, bestFreqFT);

	// snprintf((*output).str, OUTPUT_LEN,
	// 	"%10.3f::  first: %10.3f Hz, quality %3.0f%%; best: %10.3f Hz, quality %3.0f%%, index %2d; bestFT: %10.3f Hz, quality %3.0f%%; %s",
	// 	(double)streamPos/playerSampleRate,
	// 	firstFreq, firstQuality,
	// 	bestFreq, bestQuality, bestQualityIndex,
	// 	bestFreqFT, bestQualityFT,
	// 	toneName);

	snprintf((*output).str, OUTPUT_LEN,
		"%s   %10.3f Hz   %3.0f%%",
		toneName, bestFreqFT, bestQualityFT);

	(*output).quality=bestQualityFT;

	//free(buffer);
}

bool taskOutFunc() {
	bool ret = false;
	for (; outputPos < pos; outputPos++) {
		if (status[outputPos % STATUS_LEN] == OUTPUT) {
			double quality = outputs[outputPos % STATUS_LEN].quality;
			char str[OUTPUT_LEN] = "";
			static int gapLen = 0;
			static int toneLen = 0;
			if (quality <= msgOption_maxGapQuality) {
				gapLen++;
				toneLen = 0;
			} else {
				gapLen = 0;
				toneLen++;
			}
			snprintf(str, OUTPUT_LEN, "%s  %10.2f s", outputs[outputPos % STATUS_LEN].str, toneLen / msgOption_outputRate);
			if (msgOption_onePerTone) {
				static double maxQuality=-1;
				static char maxQualityOut[OUTPUT_LEN] = "";
				if ((gapLen >= minGapLen) && (maxQualityOut[0] != '\0')) {
					printf("%s\n", maxQualityOut);
					maxQualityOut[0] = '\0';
					maxQuality=-1;
				}
				if (quality >= msgOption_minQuality) {
					if (quality >= maxQuality) {
						maxQuality = quality;
						strncpy(maxQualityOut, str, OUTPUT_LEN);
					}
				}
			} else {
				if (quality >= msgOption_minQuality) {
					printf("%s\n", str);
				}
			}
			status[outputPos % STATUS_LEN] = DONE;
			__sync_synchronize();
			ret = true;
		} else break;
	}
	return ret;
}

bool taskFunc() {
	int reservedPos = -1;
	int i = pos - STATUS_LEN + 1;
	if (i < 0) i = 0;
	for (; i < pos; i++) {
		if (__sync_bool_compare_and_swap(status + (i % STATUS_LEN), FREE, RESERVED)) {
			reservedPos = i;
			break;
		}
	}
	if (reservedPos < 0) {
		reservedPos = __sync_fetch_and_add(&pos, 1);
		if (!__sync_bool_compare_and_swap(status + (reservedPos % STATUS_LEN), DONE, RESERVED)) {
			printf("Cannot reserve...\n");
			return false;
		}
		status[reservedPos % STATUS_LEN] = RESERVED;
	}
	//printf("Reserved %d\n", reservedPos);
	int streamPos = reservedPos * (int)(shiftSec * playerSampleRate);
	if (streamPos + blockSize > playerBuffer.end) {
		status[reservedPos % STATUS_LEN] = FREE;
		//printf("Freed %d\n", reservedPos);
		return false;
	}

	processBlock(streamPos, outputs + (reservedPos % STATUS_LEN));

	status[reservedPos % STATUS_LEN] = OUTPUT;

	return true;
}

static __attribute__((constructor)) void init() {
	for (int i = 0; i < STATUS_LEN; i++) {
		status[i] = DONE;
	}
	tmTaskRegister(&prOutTask, taskOutFunc, 10);
	tmTaskRegister(&prTask, taskFunc, 11);
	printf("Pitch recognition module loaded...\n");
}

void prNewSource() { // to be called only once
	shiftSec      = 1/msgOption_outputRate;
	blockSizeLog  = ceil(log2(msgOption_blockMs*playerSampleRate/1000));
	blockSize     = 1ll << blockSizeLog;
	blockTailSize = msgOption_subblockMs*playerSampleRate/1000;
	blockRetSize  = blockSize - blockTailSize + 1;
	maxAllowedPeriod = blockTailSize;
	if (maxAllowedPeriod > blockRetSize-1) {
		maxAllowedPeriod = blockRetSize-1;
	}
	minGapLen = msgOption_outputRate * msgOption_minGap / 1000;

	double precision = blockRetSize-blockSize/ceil(blockSize/maxAllowedPeriod);
	precision = log2(precision / (precision-1)) * 1200;

	double lowestFreq = playerSampleRate / maxAllowedPeriod;
	char toneName[30];
	toneNameCents(toneName, lowestFreq);

	fprintf(stderr, "\n"
		"sample rate:     %10.3f Hz\n"
		"output rate:     %10.3f Hz (%10.3f ms)\n"
		"block size log2: %10d\n"
		"block size:      %10d    (%10.3f ms)\n"
		"subblock size:   %10d    (%10.3f ms)\n"
		"lowest freq:     %10.3f Hz (%s )\n"
		"precision:       %10.3f cents\n\n",
		playerSampleRate,
		msgOption_outputRate, shiftSec*1000,
		blockSizeLog,
		blockSize, blockSize / playerSampleRate * 1000,
		blockTailSize, blockTailSize / playerSampleRate * 1000,
		lowestFreq, toneName,
		precision);
	bufferPerThread = memPerThreadMalloc(sizeof(float) * blockSize);
	prTask.active = true;
	prOutTask.active = true;
}
