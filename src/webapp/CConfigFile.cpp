#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/NodeIterator.h>
#include <Poco/DOM/NodeFilter.h>
#include <Poco/DOM/AutoPtr.h>
#include <Poco/SAX/InputSource.h>
#include <Poco/XML/XMLWriter.h>

#include <fstream>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include"CConfigFile.h"


CConfigFile::CConfigFile(const std::string& _filename)
{
    filename = _filename;
    Load();
}

void CConfigFile::Load()
{
    std::ifstream in(filename.c_str());
    if (!in.is_open())
    {
        // error and create new file
        Save();
        return;
    }
    
    Poco::XML::InputSource src(in);
    Poco::XML::DOMParser parser;
    Poco::AutoPtr<Poco::XML::Document> pDoc = parser.parse(&src);
    
    Poco::XML::NodeIterator it(pDoc, Poco::XML::NodeFilter::SHOW_ALL);
    Poco::XML::Node* pNode = it.nextNode();
    while (pNode)
    {
        //std::cout << pNode->nodeName() << ":" << pNode->getNodeValue() << std::endl;
        pNode = it.nextNode();
    }
}

void CConfigFile::Save()
{
    std::ofstream str(filename + ".temp");
    if (!str.is_open())
    {
        // error, just return
        return;
    }
    Poco::XML::XMLWriter writer(str, Poco::XML::XMLWriter::WRITE_XML_DECLARATION | Poco::XML::XMLWriter::PRETTY_PRINT);
    writer.setIndent("    ");
    writer.setNewLine("\n");
    writer.startDocument();
    writer.startElement("", "configuration", "");
    writer.startElement("", "webinterface", "");
    writer.dataElement("", "host", "", hostname);
    writer.dataElement("", "port", "", std::to_string(port));
    writer.endElement("", "webinterface", "");
    writer.dataElement("", "mountpoint", "", mountpoint);
    writer.endElement("", "configuration", "");
    writer.endDocument();
    str.close();
    
    int fd = open((filename + ".temp").c_str(), O_APPEND);
    fsync(fd);
    close(fd);
    
    rename((filename + ".temp").c_str(), filename.c_str());
}
/*
int main()
{
    CConfigFile config("CoverFS.config");
    config.Save();
    
    return 0;
}
*/




