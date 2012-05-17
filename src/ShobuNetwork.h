#ifndef SHOBU_NETWORK_H
#define SHOBU_NETWORK_H

#ifdef WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


#include <deque>
#include <vector>

class ShobuNetwork
{
    public:
    ShobuNetwork();
    ~ShobuNetwork();


    /*!
     * \brief initializeHost() creates a new network host that waits for a connection
     * \param port The port on which to host a connection
     * \return false on failure, true on success
     */
    bool initializeHost(int port);

    /*!
     * \brief initializeClient() creates a new network client that attempts to connect to a remote host
     * \param ip_addr
     * \param port
     * \return false on failure, true on success
     */
    bool initializeClient(const char* ip_addr, int port);

    void waitForClient();
    void connectToHost();
    void networkUpdate();

    void sendInput();
    void sendInputRequest();

    void addInputState(int state);

    int getState();

    void setInputDelay(int delay);

    void setLocalTick(int tick);
    int remoteTick();

    void disconnect();
    bool connected();

    void lock();
    void unlock();
private:
    // Prevent the creation of copies of the ShobuNetwork instance
    ShobuNetwork& operator= (const ShobuNetwork&) { }


    ShobuNetwork(const ShobuNetwork&) {}

    /*!
     * \brief createSocket() is a utility function used internally by ShobuNetwork to create a socket;
     * \param port
     * \return false on failure, true on success
     */
    bool createSocket(int port);

    int m_port; /// Port which the connection is open on
    int m_socket;  /// Socket which the connection is bound to

    struct sockaddr_in m_host_address;  /// Address of the remote host

    struct sockaddr_in m_remote_addr;  /// Address of client

    std::deque<int> m_input_buffer; /// The current input buffer
    std::vector<int> m_remote_input_buffer;
    int m_input_buffer_size;

    int m_delay;  /// number of frames of input delay
    int m_sync;   /// the frame of the input buffer used for syncing (= m_input_buffer_size - m_delay - 1)

    int m_remote_tick; /// Current tick of the remote game
    int m_local_tick;  /// Current tick of the local game

    bool m_connected;  /// flag that keeps track of the status of the remote connection

    char m_client; /// flag indicating client or server.  'c'=client, 's'=server

    bool m_locked;  ///  used to prevent another thread from accessing resources

};
#endif // SHOBU_NETWORK_H

