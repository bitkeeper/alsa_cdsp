/*
 * BlueALSA - rt.h
 * Copyright (c) 2016-2021 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "rt.h"

#include <stdlib.h>

/**
 * Synchronize time with the sampling rate.
 *
 * Notes:
 * 1. Time synchronization relies on the frame counter being linear.
 * 2. In order to prevent frame counter overflow (for more information see
 *   the asrsync structure definition), this counter should be initialized
 *   (zeroed) upon every transfer stop.
 *
 * @param asrs Pointer to the time synchronization structure.
 * @param frames Number of frames since the last call to this function.
 * @return This function returns a positive value or zero respectively for
 *   the case, when the synchronization was required or when blocking was
 *   not necessary. If an error has occurred, -1 is returned and errno is
 *   set to indicate the error. */
int asrsync_sync(struct asrsync *asrs, unsigned int frames) {

	const unsigned int rate = asrs->rate;
	struct timespec ts_rate;
	struct timespec ts_now;
	struct timespec ts_running;
	struct timespec* ts_rev = 0;
	int rv = 0;
	const uint64_t FRAME_THRESHOLD = 200000;

	asrs->frames += frames;

	/* There is an issue when using asrs->ts0 and asrs->frames directly from the start.
	 * This result is stutter after the start (on pause/play).
	 * Not sure why,if the asrs>ts and frames is used, then the problem isn't there.
	 * But that gives XRUNs on long term play > 3 hours.
	 *
	 * Workarround:
	 * The asrs->sync_mode toggles between those more after FRAME_THRESHOLD after startup.
	 */
	if(!asrs->sync_mode) {
		asrs->sync_mode = asrs->frames >= FRAME_THRESHOLD;
		if(asrs->sync_mode) {
			debug("Synced mode active\n");
			asrs->frames = frames;
			asrs->ts0 = asrs->ts;
		}
	}

	if(asrs->sync_mode) {
		frames = asrs->frames;
		ts_rev = &asrs->ts0;
	}else {
		ts_rev = &asrs->ts;
	}

	ts_rate.tv_sec = frames / rate;
	ts_rate.tv_nsec = 1000000000L / rate * (frames % rate);

	gettimestamp(&ts_now);
	/* calculate delay since the last sync */
	timespecsub(&ts_now, &asrs->ts, &asrs->ts_busy);

	/* maintain constant rate */
	timespecsub(&ts_now, ts_rev, &ts_running);
	if (difftimespec(&ts_running, &ts_rate, &asrs->ts_idle) > 0) {
		if (!asrs->sync_mode) {
			double idle_time = (double)asrs->ts_idle.tv_sec + (double)asrs->ts_idle.tv_nsec/1e9;
			idle_time *= 0.5;
			asrs->ts_idle.tv_sec = (time_t)idle_time;
			asrs->ts_idle.tv_nsec = (long)(idle_time*1e9);
		}

		nanosleep(&asrs->ts_idle, NULL);
		rv = 1;
	}

	gettimestamp(&asrs->ts);
	return rv;
}

/**
 * Calculate time difference for two time points.
 *
 * @param ts1 Address to the timespec structure providing t1 time point.
 * @param ts2 Address to the timespec structure providing t2 time point.
 * @param ts Address to the timespec structure where the absolute time
 *   difference will be stored.
 * @return This function returns an integer less than, equal to, or greater
 *   than zero, if t2 time point is found to be, respectively, less than,
 *   equal to, or greater than the t1 time point.*/
int difftimespec(
		const struct timespec *ts1,
		const struct timespec *ts2,
		struct timespec *ts) {

	const struct timespec _ts1 = *ts1;
	const struct timespec _ts2 = *ts2;

	if (_ts1.tv_sec == _ts2.tv_sec) {
		ts->tv_sec = 0;
		ts->tv_nsec = labs(_ts2.tv_nsec - _ts1.tv_nsec);
		return _ts2.tv_nsec - _ts1.tv_nsec;
	}

	if (_ts1.tv_sec < _ts2.tv_sec) {
		timespecsub(&_ts2, &_ts1, ts);
		return 1;
	}

	timespecsub(&_ts1, &_ts2, ts);
	return -1;
}
