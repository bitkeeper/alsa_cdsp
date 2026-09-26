#ifndef CDSP_PIPE_WRITE_H
#define CDSP_PIPE_WRITE_H

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * cdsp_pipe_write_period() - write one whole PCM period to the CamillaDSP FIFO,
 * atomically with respect to THREAD CANCELLATION.
 *
 * The IO thread streams to CamillaDSP one period (a whole number of frames) at a
 * time; the ioplug .stop callback (fired by snd_pcm_drop()) pthread_cancel()s
 * that thread. write() is a *deferred cancellation point*, so a cancel arriving
 * while a period is only PARTIALLY written would terminate the thread mid-write,
 * leaving a partial, sub-frame byte count in the FIFO. The still-running
 * CamillaDSP then keeps reading, now misaligned by < 1 frame, and every sample
 * is rebuilt from bytes straddling two real samples => permanent wideband noise
 * until the pipe is torn down.
 *
 * Disabling cancellation across the write loop defers any pending cancel to the
 * next cancellation point (the caller's loop, once cancellation is restored), so
 * the FIFO always ends on a frame boundary. The reader (CamillaDSP) drains the
 * pipe continuously, so this completes in bounded time; if the reader has closed
 * its end, write() returns EPIPE (SIGPIPE is blocked by the IO thread) and we
 * return -1 so the caller can tear down.
 *
 * Returns 0 when all `len` bytes were written, or -1 with errno set (EPIPE when
 * the reader is gone). `len` must be a whole number of frames.
 */
static inline int cdsp_pipe_write_period(int fd, const char *buf, size_t len) {
	int oldstate;
	ssize_t ret;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	while (len != 0) {
		if ((ret = write(fd, buf, len)) == -1) {
			if (errno == EINTR)
				continue;
			pthread_setcancelstate(oldstate, NULL);
			return -1; /* caller inspects errno (EPIPE = reader closed) */
		}
		buf += ret;
		len -= (size_t)ret;
	}
	pthread_setcancelstate(oldstate, NULL);
	return 0;
}

#endif /* CDSP_PIPE_WRITE_H */
