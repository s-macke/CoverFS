#ifndef DEBUG_H
#define DEBUG_H

#include <sstream>

enum LogLevel {ERROR, WARN, INFO, DEBUG, DEEP};

class Logger
{
    public:
    Logger();
    ~Logger();

    std::ostringstream& Get(LogLevel);

    static LogLevel& GetReporingLevel();

    void Set(LogLevel newlevel);

    private:
        std::string ToString(LogLevel level);
        static LogLevel level;
        std::ostringstream os;
};

#define LOG(level)\
  if (level > Logger::GetReporingLevel()) ;\
  else Logger().Get(level)

#endif
