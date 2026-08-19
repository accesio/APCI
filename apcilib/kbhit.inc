/*
 * SPDX-FileCopyrightText: 2019 Stack Exchange, Inc.
 *
 * SPDX-License-Identifier: CC-BY-SA-4.0
 */

//https://stackoverflow.com/questions/29335758/using-kbhit-and-getch-on-linux
//retrieved 2019-11-23
#include <stdbool.h>
#include <sys/ioctl.h>
#include <termios.h>

bool kbhit()
{
    struct termios term;
    tcgetattr(0, &term);

    struct termios term2 = term;
    term2.c_lflag &= ~ICANON;
    tcsetattr(0, TCSANOW, &term2);

    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);

    tcsetattr(0, TCSANOW, &term);

    return byteswaiting > 0;
}