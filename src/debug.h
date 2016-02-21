#ifndef DEBUG_H
#define DEBUG_H

#include<iostream>

class Debug
{
    public:
    //Debug(int _level=WARN) : level(_level) {};
    Debug(int _level=WARN);

    template<typename T>
    Debug& operator<<(T const& t)
    {
        if (level <= currentlevel)
        {
            std::cout << t;
        }
        return *this;
    }

    void Set(int newlevel);

    static const int ERR  = 0;
    static const int WARN = 1;
    static const int INFO = 2;

    private:
        int level;
        int& currentlevel;
};

#endif
