#pragma once

#include <termios.h>

#include "isa.hpp"

// The emulated terminal's half of the host tty. Keys must arrive unbuffered
// and unechoed the moment they're pressed, which isn't how a tty behaves by
// default, so the constructor puts it in raw mode. The destructor restores it
// on a normal exit. A fatal signal (ctrl+C, ctrl+\, kill) skips destructors,
// so a signal handler restores it in that case.
class Terminal {
public:
    Terminal();
    ~Terminal();

    // ASCII code of a key pressed since the last call, or -1 if none was.
    // Never blocks; the emulation has to keep running while nobody types.
    int readKey();

    void writeChar(Byte ascii);

private:
    bool raw_mode = false;      // false when stdin is not a tty, a pipe say
    int saved_flags = -1;
    termios saved_attributes{};

    // Undoes what the constructor changed. Safe to call twice, since the
    // signal handler can interrupt the destructor halfway through it.
    void restore();
    static void onSignal(int signal_number);
};
