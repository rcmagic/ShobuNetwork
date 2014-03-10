#include <fstream>
#include <string>
#include <sstream>
#include <list>

#ifndef LOGGER_H
#define LOGGER_H

enum LogType { Warning, Error, Debug, Message };
class NetworkLogger
{
    public:



	// Destructor 
    ~NetworkLogger();

	// Precondition:
	// 	returns a pointer to the only instance of this class
    static NetworkLogger* getInstance();

    void setPlace(const char* file, const char* func, const int line, LogType type);

	// print a message to the log file
	template <typename T>
	    void printMessage(const T &obj)
        {
            if(event_type == Message) {
                message_string << obj;
            }

            log_file << obj;
	    }
	void printMessage(const char c);

    // total is the number of latest lines to return
    // Return a string of the current list of messages
    std::string getMessageLog(int total);

    // Set the scroll offset the returned log string
    void scrollUp();
    void scrollDown();

private:
    // create logger instance
    NetworkLogger();

	// pointer to the single instance of this class
    static NetworkLogger* instance;

	// log file handle
	std::ofstream log_file;

    std::stringstream log_string, message_string;
    std::list<std::string> message_lines;

	// current File name
    const char* file_name;

	// current Function  
	std::string function;

	// current Line Number
	int line_number;

	// Event type
    LogType event_type;

	// End of section flag
	bool end_section;

	// keeps up with the event id
	unsigned int id;

    // line offset
    int m_offset;

    // amount of lines to scroll up or down
    int m_scrollAmount;
};


// Below I'm essential defining 4 streams to ouput debug info
// error is used to output critical error that cause the program to end
// warning is used to output non-fatal error information
// debug is used to output non-error progress information 
// message is used to output game messages

// defines a macro to store source code information when printing a msg
#define LogError \
(NetworkLogger::getInstance()->setPlace(__FILE__, __FUNCTION__,\
 __LINE__, LogType::Error), \
    *NetworkLogger::getInstance()) << "Error (" << __FILE__ << " " << __LINE__ << "): "

#define LogWarning \
(NetworkLogger::getInstance()->setPlace(__FILE__, __FUNCTION__,\
 __LINE__, LogType::Warning), \
    *NetworkLogger::getInstance())<< "Warning (" << __FILE__ << " " << __LINE__ << "): "
#define LogDebug \
(NetworkLogger::getInstance()->setPlace(__FILE__,  __FUNCTION__,\
 __LINE__, LogType::Debug), \
 *NetworkLogger::getInstance()) << "Debug (" << __FILE__ << " " << __LINE__ << "): "

#define LogMessage \
(NetworkLogger::getInstance()->setPlace(__FILE__, __FUNCTION__,\
__LINE__, LogType::Message), \
*NetworkLogger::getInstance())

#define LogNull *NetworkLogNothing::getInstance()

template <typename T>
NetworkLogger& operator<<(NetworkLogger& s, const T& obj)
{
    s.printMessage(obj);
    return s;
}


class NetworkLogNothing
{
    public:
    static NetworkLogNothing* getInstance();
    private:
    NetworkLogNothing();
    static NetworkLogNothing* instance;

};

template <typename T>
NetworkLogNothing& operator<<(NetworkLogNothing& s, const T& obj)
{
    return s;
}
#define endline '\n'

#endif // LOGGER_H
