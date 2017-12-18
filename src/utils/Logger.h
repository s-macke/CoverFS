#ifndef DEBUG_H
#define DEBUG_H

#include <sstream>

enum class LogLevel : int {ERR, WARN, INFO, DEBUG, DEEP};

class Logger
{
    public:
    Logger();
    ~Logger();

    std::ostringstream& Get(LogLevel);

    static LogLevel& GetReporingLevel();

    void Set(LogLevel newlevel);

    template <typename E>
    static constexpr auto to_underlying(E e) noexcept
    {
      return static_cast<std::underlying_type_t<E>>(e);
    }


private:
        std::string ToString(LogLevel level);
        static LogLevel level;
        std::ostringstream os;
};

#define LOG(level)\
  if (Logger::to_underlying(level) > Logger::to_underlying(Logger::GetReporingLevel())) ;\
  else Logger().Get(level)

#endif
