#pragma once

#include <termios.h>

#include "isa.hpp"

// The emulated terminal's half of the host tty. Keys must arrive unbuffered
// and unechoed the moment they're pressed, which isn't how a tty behaves by
// default, so the constructor puts it in raw mode and the destructor always
// restores it, no matter how we exit.
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
};
