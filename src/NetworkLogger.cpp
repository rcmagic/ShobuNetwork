#include "NetworkLogger.h"

// init the instance pointer
NetworkLogger* NetworkLogger::instance = 0;
NetworkLogger::NetworkLogger()
{
    id = 1;

    end_section = true;

    // Open Log File to write to
    log_file.open("network_log.txt");

    m_offset = 0;

    m_scrollAmount = 1;
}

// returns the only Logger instance
NetworkLogger* NetworkLogger::getInstance()
{
    static NetworkLogger obj;
    instance = &obj;

    return instance;
}

NetworkLogger::~NetworkLogger()
{
    log_file.flush();
    // free instance pointer
    instance = 0;
}



void NetworkLogger::setPlace(const char* file,	const char* func, const int line,
    LogType type)
{
    file_name = file;
    function = func;
    line_number = line;
    event_type = type;
}
void NetworkLogger::printMessage(const char c)
{
    if(event_type == Message) {
        message_string << c;
    }

    if(c == '\n'){
        end_section = true;
        log_file << '\n';
        log_file.flush();

        if(event_type == Message) {
            message_lines.push_back(message_string.str());
            message_string.str("");
            m_offset = message_lines.size();
        }

    } else {
        log_file << c;
    }
}

std::string NetworkLogger::getMessageLog(int total)
{
    std::string lines;
    int begin = 0;

    auto it = message_lines.begin();

    if(m_offset > total) {
        std::advance(it, m_offset - total);
    }

    int i = 0;
    while(it != message_lines.end() && i < total) {
        lines.append(*it);
        it++;
        i++;
    }

    return lines;
}

void NetworkLogger::scrollUp()
{
    m_offset -= m_scrollAmount;
    if(m_offset < 0) {
        m_offset = 0;
    }
}

void NetworkLogger::scrollDown()
{
    m_offset += m_scrollAmount;
    if(m_offset > message_lines.size()) {
        m_offset = message_lines.size();
    }
}

NetworkLogNothing* NetworkLogNothing::instance = 0;
NetworkLogNothing::NetworkLogNothing() {}
NetworkLogNothing* NetworkLogNothing::getInstance()
{
    static NetworkLogNothing obj;
    instance = &obj;

    return instance;
}
