#include <Poco/Util/ServerApplication.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Util/ServerApplication.h>

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include<algorithm>
#include<string>
#include<sstream>
#include<vector>
#include<iostream>
#include<fstream>

#include"CConfigFile.h"
#include"StringUtils.h"

// compile with g++ main.cpp CConfigFile.cpp -lPocoNet -lPocoUtil -lPocoFoundation -lPocoXML

class CStatusbar {
    public:
    std::string status, message;
    CStatusbar(std::string _status, std::string _message) : status(_status), message(_message) {}
};

CConfigFile configfile("CoverFS.config");
std::vector<CStatusbar> statusbar;

class RequestHandler : public Poco::Net::HTTPRequestHandler
{
public:

virtual void handleRequest(Poco::Net::HTTPServerRequest &req, Poco::Net::HTTPServerResponse &resp)
{

    std::cout << " URI=" << req.getURI() << std::endl;
    std::vector<std::string> paths = split(req.getURI(), '/');
    std::vector<std::string> parameters;
    std::vector<std::string> lastpathsplit = split(paths.back(), '?');

    if (lastpathsplit.size() == 2)
    {
        paths.back() = lastpathsplit.front();
        parameters = split(lastpathsplit.back(), '&');
        std::cout << parameters.size() << std::endl;
    }

    if ((paths.size() == 3) && (paths[1] == "css"))
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("text/css");
        std::ostream& out = resp.send();
        SendFile(std::string("templates/css/") + paths[2], out);
        out.flush();
    } else
    if ((paths.size() == 3) && (paths[1] == "js"))
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("text/javascript");
        std::ostream& out = resp.send();
        SendFile(std::string("templates/js/") + paths[2], out);
        out.flush();
    } else
    if ((paths.size() == 3) && (paths[1] == "ajax"))
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("application/json");
        std::ostream& out = resp.send();
        if (paths[2] == "status")
        {
            out << "{";
            out << "\"downloadrate\" : \"" << (rand()%50) << " MB/s\",";
            out << "\"uploadrate\" : \"" << (rand()%50) << " MB/s\"";
            /*
            cachedblocks
            writecache
            ncommits
            ninodes
            fragments
            fragmentation
            containersize
            emptyspace
            */
            out << "}";
        }
        if (paths[2] == "log")
        {
            out << "{}";
        }
        if (paths[2] == "mount")
        {
            statusbar.push_back(CStatusbar("success", "Container mounted on W:"));
            out << "{}";
        }
        if (paths[2] == "scan")
        {
            statusbar.push_back(CStatusbar("success", "Scan started. Please consult the log for more details"));
            out << "{}";
        }
        if (paths[2] == "optimize")
        {
            statusbar.push_back(CStatusbar("danger", "Optimize destroyed your container"));
            out << "{}";
        }
        if (paths[2] == "shrink")
        {
            statusbar.push_back(CStatusbar("info", "Sorry, Shrink is not yet supported"));
            out << "{}";
        }
        if (paths[2] == "statusbar")
        {
            out << "{";
            if (statusbar.size() >= 1)
            {
                out << "\"status\" : \"" << statusbar[0].status << "\",";
                out << "\"message\" : \"" << statusbar[0].message << "\"";
                statusbar.erase(statusbar.begin());
            }
            out << "}";
        }
        if ((paths[2] == "config") && (parameters.size() == 4))
        {
            std::cout << parameters[0] << std::endl;
            std::cout << configfile.hostname << std::endl;
            out << "{";
            out << "\"host\" : \"" << configfile.hostname << "\",";
            out << "\"port\" : \"" << configfile.port << "\",";
            out << "\"mountpoint\" : \"" << configfile.mountpoint << "\"";
            out << "}";
        }
        out.flush();
    } else
    if ((paths.size() == 2) && (EndsWith(paths[1], ".html")))
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        resp.setContentType("text/html");
        std::ostream& out = resp.send();
        SendFile("templates/header.html", out);

        std::string filename = split(paths[1], '.')[0];
        std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
        out << "<script>function SetNavbarActive(){$(\"#" + filename + "navicon.nav-item\").addClass(\"active\");}</script>";

        SendFile(std::string("templates/") + paths[1], out);
        SendFile("templates/footer.html", out);
        out.flush();
    } else
    if (paths.size() == 1)
    {
        resp.redirect("configuration.html");
        resp.send();
    } else
    {
        resp.setStatus(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        resp.setContentType("text/html");
        std::ostream& out = resp.send();
        out << "<h1>404 not found</h1>";
        out.flush();
    }
  }

    private:

    void SendFile(const std::string& filename, std::ostream& out)
    {
        std::ifstream ifs(filename.c_str());
        if (!ifs.is_open()) return;
        out << ifs.rdbuf();
    }

};

class HandlerFactory : public Poco::Net::HTTPRequestHandlerFactory
{
    public:
    HandlerFactory () {}

    Poco::Net::HTTPRequestHandler* createRequestHandler (const Poco::Net::HTTPServerRequest &request)
    {
        return new RequestHandler();
    }
};


class SimpleHTTPServerApplication : public Poco::Util::ServerApplication
{
    protected:
    int main(const std::vector<std::string> &args)
    {
        Poco::UInt16 port = 9999;
        Poco::Net::ServerSocket socket(port);
        Poco::Net::HTTPServerParams *pParams = new Poco::Net::HTTPServerParams();

        Poco::Net::HTTPServer server(new HandlerFactory(), socket, pParams);

        server.start();
        waitForTerminationRequest();
        server.stop();
        return EXIT_OK;
    }
};

int StartWebApp()
{
    int argc = 1;
    const char* argv_const[] = {"CoverFS", NULL};
    char **argv = const_cast<char**>(argv_const);

    try
    {
        SimpleHTTPServerApplication app;
        return app.run(argc, argv);
    }
    catch (Poco::Exception& exc)
    {
        std::cerr << exc.displayText() << std::endl;
        return Poco::Util::Application::EXIT_SOFTWARE;
    }
}
