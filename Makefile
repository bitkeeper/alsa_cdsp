CC = gcc
CFLAGS += -Wall -Wextra -fPIC -DPIC 
#LDFLAGS += -Wall -shared -lasound
#LDFLAGS below is needed in Debian Buster for certain sound players
LDFLAGS += -Wall -shared -Wl,--no-as-needed -lasound -Wl,--as-needed
#LIBDIR := lib/$(shell gcc --print-multiarch)
LIBDIR := $(shell pkg-config --variable=libdir alsa)

TARGET = libasound_module_pcm_cdsp

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET).so $(TARGET).c

install:
	mkdir -p  $(LIBDIR)/alsa-lib/
	install -m 644 $(TARGET).so $(LIBDIR)/alsa-lib/

uninstall:
	rm $(LIBDIR)/alsa-lib/$(TARGET).so

# Cancellation-safety regression test for the FIFO writer. Exercises
# cdsp_pipe_write.h directly (no ALSA/CamillaDSP needed): it repeatedly
# pthread_cancel()s a writer feeding a small pipe with a slow reader and asserts
# the FIFO always ends on a frame boundary. Build with -DUNGUARDED to observe
# the sub-frame byte slip the guard prevents.
TEST = test/frame_align_on_cancel

test: $(TEST)
	$(TEST)

$(TEST): $(TEST).c cdsp_pipe_write.h
	$(CC) $(CFLAGS) -O2 -pthread -o $(TEST) $(TEST).c

clean:
	rm -f $(TARGET).so $(TEST)
