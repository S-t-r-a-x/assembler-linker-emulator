#include "../../inc/terminal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

Terminal::Terminal() {
    // Non-blocking first, and unconditionally: raw mode's VMIN = 0 only covers
    // stdin being a tty, and a read from a pipe with nothing in it would
    // otherwise stall the emulation until something arrived.
    saved_flags = fcntl(STDIN_FILENO, F_GETFL);
    if (saved_flags != -1)
        fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);

    if (tcgetattr(STDIN_FILENO, &saved_attributes) != 0)
        return;                 // not a tty, so there are no attributes to change

    termios raw = saved_attributes;
    // ICANON off delivers each key as it is pressed rather than a line at a
    // time, ECHO off keeps the terminal from displaying it. ISIG stays on
    // deliberately, so ctrl+C can still stop a runaway user program.
    raw.c_lflag &= ~(ICANON | ECHO);
    // Return whatever is there and never wait for more.
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    raw_mode = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
}

Terminal::~Terminal() {
    if (raw_mode)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_attributes);
    if (saved_flags != -1)
        fcntl(STDIN_FILENO, F_SETFL, saved_flags);
}

int Terminal::readKey() {
    unsigned char key = 0;
    return ::read(STDIN_FILENO, &key, 1) == 1 ? key : -1;
}

void Terminal::writeChar(Byte ascii) {
    // Flushed per character: the user program's output has to appear as it is
    // produced, not once the emulation ends.
    std::cout << static_cast<char>(ascii) << std::flush;
}
