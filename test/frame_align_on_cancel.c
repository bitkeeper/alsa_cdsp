/*
 * frame_align_on_cancel.c - regression test for the FIFO-writer cancellation
 * hazard fixed by cdsp_pipe_write.h.
 *
 * The plugin's IO thread streams to CamillaDSP one PCM period (a whole number of
 * frames) at a time, and the .stop callback (snd_pcm_drop()) pthread_cancel()s
 * that thread. write() is a deferred cancellation point, so a cancel landing
 * mid-period could leave a PARTIAL, sub-frame byte count in the pipe; CamillaDSP
 * then reads the byte stream misaligned by < 1 frame => permanent wideband hash.
 *
 * This test reproduces exactly that pressure with no ALSA/CamillaDSP needed: a
 * writer thread pushes fixed-size periods into a deliberately small pipe while a
 * slow reader drains it (so the writer is usually blocked mid-period), and the
 * main thread pthread_cancel()s the writer at a random instant. It then drains
 * the pipe and asserts the total byte count is always a whole number of frames.
 *
 *   make test                         # guarded (cdsp_pipe_write_period) -> PASS
 *   cc -DUNGUARDED ... frame_align_on_cancel.c   # naive write loop -> FAILs,
 *                                     # demonstrating the slip the guard prevents
 *
 * A non-power-of-two frame (6 bytes = stereo S24_3LE, the moOde/qbzd case) makes
 * any sub-frame slip visible: a partial period is almost never a multiple of 6.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cdsp_pipe_write.h"

#define FRAME 6                       /* stereo S24_3LE */
#define PERIOD_FRAMES 4096            /* period >> pipe so writes block mid-period */
#define PERIOD (FRAME * PERIOD_FRAMES)
#define PIPE_BYTES 4096               /* one page: the plugin's deliberately tiny FIFO */
#define ITERS 300

#ifdef UNGUARDED
/* Naive (pre-fix) period write: write() stays a cancellation point, so a cancel
 * can terminate the thread mid-period and leave a partial sub-frame behind. */
static int write_period_naive(int fd, const char *buf, size_t len) {
	ssize_t r;
	while (len != 0) {
		if ((r = write(fd, buf, len)) == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += r;
		len -= (size_t)r;
	}
	return 0;
}
#  define WRITE_PERIOD write_period_naive
#else
#  define WRITE_PERIOD cdsp_pipe_write_period
#endif

static char g_period[PERIOD];

struct reader_ctx { int fd; size_t total; };

/* Slow consumer: small reads with a short delay, so the writer spends most of
 * its time blocked inside a period write - the window the cancel must be safe in.
 * Drains to EOF (after the write end is closed) so `total` == everything the
 * writer actually committed to the FIFO. */
static void *reader_fn(void *arg) {
	struct reader_ctx *c = arg;
	char b[500];                       /* non-frame-multiple read size, adversarial */
	for (;;) {
		ssize_t n = read(c->fd, b, sizeof b);
		if (n > 0) {
			c->total += (size_t)n;
			usleep(60);
		} else if (n == 0) {
			break;                     /* write end closed, fully drained */
		} else if (errno == EINTR) {
			continue;
		} else {
			break;
		}
	}
	return NULL;
}

static void *writer_fn(void *arg) {
	int fd = *(int *)arg;
	for (;;) {
		if (WRITE_PERIOD(fd, g_period, PERIOD) == -1)
			break;                     /* reader gone (EPIPE) */
		pthread_testcancel();          /* honour a pending cancel on a frame boundary */
	}
	return NULL;
}

int main(void) {
	memset(g_period, 0xA5, sizeof g_period);
	srand(20260910u);

	size_t fails = 0, exercised = 0;
	for (int i = 0; i < ITERS; i++) {
		int fd[2];
		if (pipe(fd) != 0) { perror("pipe"); return 2; }
		(void)fcntl(fd[1], F_SETPIPE_SZ, PIPE_BYTES);

		struct reader_ctx rc = { fd[0], 0 };
		pthread_t tr, tw;
		pthread_create(&tr, NULL, reader_fn, &rc);
		pthread_create(&tw, NULL, writer_fn, &fd[1]);

		usleep(200 + rand() % 1500);   /* let the writer get mid-period, then... */
		pthread_cancel(tw);            /* ...the drop */
		pthread_join(tw, NULL);
		close(fd[1]);                  /* reader drains the remaining FIFO to EOF */
		pthread_join(tr, NULL);
		close(fd[0]);

		if (rc.total > 0)
			exercised++;
		if (rc.total % FRAME != 0) {
			if (fails < 5)
				fprintf(stderr, "  iter %d: %zu bytes in FIFO, %% %d = %zu (MISALIGNED)\n",
				        i, rc.total, FRAME, rc.total % FRAME);
			fails++;
		}
	}

	printf("frame_align_on_cancel: %d iters, %zu with data, frame=%d B, %zu misaligned\n",
	       ITERS, exercised, FRAME, fails);
#ifdef UNGUARDED
	printf("  [UNGUARDED build] exercising the pre-fix naive write loop\n");
#endif
	if (fails != 0) {
		printf("FAIL: a mid-write cancel left the FIFO non-frame-aligned\n");
		return 1;
	}
	printf("PASS: every cancel left the FIFO frame-aligned\n");
	return 0;
}
