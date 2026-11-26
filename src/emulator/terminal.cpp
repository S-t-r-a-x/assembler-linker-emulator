#include "../../inc/terminal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <iostream>

// A signal handler is a plain function, so this is how it finds the tty state.
static Terminal *active_terminal = nullptr;

Terminal::Terminal() {
    // Installed first, so the handler is in place before the tty is touched.
    active_terminal = this;
    std::signal(SIGINT, onSignal);
    std::signal(SIGQUIT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Set non-blocking unconditionally: raw mode's VMIN = 0 only helps when
    // stdin is a tty. If stdin is a pipe instead, a read would otherwise
    // block the whole emulation until data showed up.
    saved_flags = fcntl(STDIN_FILENO, F_GETFL);
    if (saved_flags != -1)
        fcntl(STDIN_FILENO, F_SETFL, saved_flags | O_NONBLOCK);

    if (tcgetattr(STDIN_FILENO, &saved_attributes) != 0)
        return;                 // not a tty, so there are no attributes to change

    termios raw = saved_attributes;
    // ICANON off: keys arrive as pressed, not buffered by line. ECHO off:
    // keystrokes aren't echoed. ISIG stays on deliberately, so ctrl+C can
    // still kill a runaway program.
    raw.c_lflag &= ~(ICANON | ECHO);
    // Return whatever is there and never wait for more.
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    raw_mode = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
}

Terminal::~Terminal() {
    restore();
    active_terminal = nullptr;
}

void Terminal::restore() {
    if (raw_mode)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_attributes);
    if (saved_flags != -1)
        fcntl(STDIN_FILENO, F_SETFL, saved_flags);
    raw_mode = false;
    saved_flags = -1;
}

// Only async-signal-safe calls in here: the signal can arrive anywhere,
// including in the middle of an iostream write, so exit() is off limits.
// Re-raising with the default action still kills the process, and the shell
// sees that it died of the signal, as it would have without this handler.
void Terminal::onSignal(int signal_number) {
    if (active_terminal)
        active_terminal->restore();
    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
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
