#include<iostream>

class CConfigFile
{
    public:
        explicit CConfigFile(const std::string& _filename);
        void Load();
        void Save();
        
        std::string hostname = "localhost";
        int port = 1234;
        std::string mountpoint = "w:";

    private:
        std::string filename;
};

