#include "Logger.h"

#include <iostream>
#include <chrono>

LogLevel Logger::level = INFO;

Logger::Logger() {}

Logger::~Logger() 
{
    os << std::endl;
    std::cout << os.str();
}

LogLevel& Logger::GetReporingLevel()
{
    return level;
}

std::ostringstream& Logger::Get(LogLevel level)
{
    //os << "- " << NowTime();
    os << " - " << ToString(level) << ": ";  
    return os;
}

void Logger::Set(LogLevel newlevel)
{
    level = newlevel;
}

std::string Logger::ToString(LogLevel level)
{
    static const char* const buffer[] = {" ERR", "WARN", "INFO", " DBG", "DEEP"};
    return buffer[level];
}
