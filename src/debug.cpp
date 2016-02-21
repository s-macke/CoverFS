#include "debug.h"
#include <iostream>

static int _currentlevel = 0;

Debug::Debug(int _level) : level(_level), currentlevel(_currentlevel)
{
}

void Debug::Set(int newlevel)
{
    _currentlevel = newlevel;
}

