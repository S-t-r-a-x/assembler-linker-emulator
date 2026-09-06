#pragma once

#include <termios.h>

#include "isa.hpp"

// The emulated terminal's half of the host tty. A keystroke has to arrive the
// moment it is pressed, unbuffered and unechoed (Project_specs.md:415), which
// is not what a terminal does by default. The constructor takes the tty over
// and the destructor hands it back, so no exit path can leave a shell in raw mode.
class Terminal {
public:
    Terminal();
    ~Terminal();

    // ASCII code of a key pressed since the last call, or -1 if none was.
    // Never blocks -- the processor has to keep running while nobody types.
    int readKey();

    void writeChar(Byte ascii);

private:
    bool raw_mode = false;      // false when stdin is not a tty, a pipe say
    int saved_flags = -1;
    termios saved_attributes{};
};
