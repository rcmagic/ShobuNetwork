#include "ShobuNetwork.h"
#include <iostream>
#include <cstring>

ShobuNetwork::ShobuNetwork()
{
    m_input_buffer_size = 6;

    // initialize buffer
    for(int i = 0; i < m_input_buffer_size; ++i) {
        m_input_buffer.push_back(0);
        m_remote_input_buffer.push_back(0);
    }

    m_connected = false;

    m_remote_tick = 0;
    m_local_tick = 0;

    m_locked = false;
}

bool ShobuNetwork::initializeHost(int port)
{

    createSocket(port);

    // Initialize Host Address
    struct sockaddr_in host_address;
    memset(&host_address, 0, sizeof(host_address));
    host_address.sin_family = PF_INET;
    host_address.sin_addr.s_addr = htonl(INADDR_ANY);
    host_address.sin_port = htons(port);

    // Bind to port
    if(bind(m_socket, (struct sockaddr*)&host_address, sizeof(host_address)) < 0 ) {
        return false;
    }

    m_port = port;

    m_client = 's';

    return true;
}


bool ShobuNetwork::initializeClient(const char* ip_addr, int port)
{
    createSocket(port);

    // Initialize remote host address
    m_host_address.sin_family = PF_INET;
    m_host_address.sin_port = htons(port);
    m_host_address.sin_addr.s_addr = inet_addr(ip_addr);

    m_client = 'c';

    return true;
}

void ShobuNetwork::waitForClient()
{
    char tmp_buffer[1];
    tmp_buffer[0] = 'a';


    struct sockaddr_in host_addr;
    socklen_t host_addr_size = sizeof(host_addr);
    char net_buffer[32];
    int recv_bytes = recvfrom(m_socket, net_buffer, 32, 0, (struct sockaddr*)&host_addr, &host_addr_size);

    switch(net_buffer[0]) {
    case 'c': // client requested a connection
        m_remote_addr = host_addr;
        // Send handshake
        for(int i=0; i<10; i++)
        sendto(m_socket, tmp_buffer, 1, 0, (sockaddr*)&m_remote_addr,
                    sizeof(struct sockaddr));
        std::cout << "Client connected, sending handshake.." << std::endl;
        m_connected = true;

        break;
    default:
        break;
    }

}

void ShobuNetwork::connectToHost()
{
    char tmp_buffer[1];
    tmp_buffer[0] = 'c';

    std::cout << "Sending handshake to the server" << std::endl;
    sendto(m_socket, tmp_buffer, 1, 0, (sockaddr*)&m_host_address,
                sizeof(struct sockaddr));

    struct sockaddr_in host_addr;
    socklen_t host_addr_size = sizeof(host_addr);
    char net_buffer[32];
    int recv_bytes = recvfrom(m_socket, net_buffer, 32, 0, (struct sockaddr*)&host_addr, &host_addr_size);

    switch(net_buffer[0]) {
    case 'a': // client requested a connection
        m_remote_addr = m_host_address;
        std::cout << "Received handshake from server.  Connected." << std::endl;

        m_connected = true;

        break;
    default:
        break;
    }
}

void ShobuNetwork::networkUpdate()
{
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof(remote_addr);
    char net_buffer[64];
    int requested_frame;
    int new_remote_tick = 0;
    // Wait for data from remote client
    int recv_bytes = recvfrom(m_socket, net_buffer, 64, 0, (struct sockaddr*)&remote_addr, &remote_addr_size);
    switch(net_buffer[0]) {
    case 'f':
        // Check to see if the remote game is not server if this is the client, and vice versa
        if(net_buffer[1] != m_client) {

            memcpy(&new_remote_tick, &net_buffer[2], 4);
            if(new_remote_tick >= m_remote_tick) {
                lock();
                m_remote_tick = new_remote_tick;
                for(int i=0; i<m_input_buffer_size; i++) {
                    int input;
                    memcpy(&input, &net_buffer[6+i*4], 4);
                    m_remote_input_buffer[i] = input;
                }
                unlock();
                std::cout << "Got tick " << m_remote_tick << " , Current tick is " << m_local_tick << std::endl;
            } else {
                std::cout << "Old remote tick sent " << std::endl;
            }

        }
        // m_remote_addr = remote_addr;

        break;
    case 'd':
        m_connected = false;
        break;
    case 'r':
        // Get the request frame
        //memcpy(&requested_frame,&net_buffer[1], 4);
        // Send the requested frame
        std::cout << "Received request for input buffer from remote client.  Sending..." << std::endl;
        sendInput();
        break;
    default:
        std::cout << "Got unknown network request" << std::endl;
        break;
    }
}

void ShobuNetwork::sendInput()
{
    char tmp_buffer[64];
    tmp_buffer[0] = 'f';
    tmp_buffer[1] = m_client;
    memcpy(&tmp_buffer[2], &m_local_tick, 4);
    for(int i=0; i<m_input_buffer.size(); i++) {
        memcpy(&tmp_buffer[6+i*4], &m_input_buffer[i], 4);
    }

    sendto(m_socket, tmp_buffer, 64, 0, (struct sockaddr*)&m_remote_addr,
                      sizeof(struct sockaddr));
}

void ShobuNetwork::sendInputRequest()
{
    char tmp_buffer[64];
    tmp_buffer[0] = 'r';
    tmp_buffer[1] = m_client;

    sendto(m_socket, tmp_buffer, 64, 0, (struct sockaddr*)&m_remote_addr,
           sizeof(struct sockaddr));
}

void ShobuNetwork::addInputState(int state)
{
    m_input_buffer.push_back(state);

    m_input_buffer.pop_front();
}

int ShobuNetwork::getState()
{
    int offset = m_sync-(m_remote_tick-m_local_tick);
    if(offset >= 0 && offset < 6) {
        return m_remote_input_buffer[offset];
    } else {
        std::cout << "Invalid input buffer offset" << offset << std::endl;
        return 0;
    }
}

void ShobuNetwork::setInputDelay(int delay)
{
    m_delay = delay;

    // m_sync is directly dependent on m_delay
    m_sync = m_input_buffer_size - delay  - 1;

}

void ShobuNetwork::setLocalTick(int tick)
{
    m_local_tick = tick;
}

int ShobuNetwork::remoteTick()
{
    return m_remote_tick;
}

void ShobuNetwork::disconnect()
{
    if(m_connected) {
#ifdef WIN32
         WSACleanup( );
#else
        close(m_socket);
#endif
    }
}

bool ShobuNetwork::connected()
{
    return m_connected;
}

void ShobuNetwork::lock()
{
    if(m_locked) {
        while(m_locked) {
        }
        m_locked = true;
    } else {
        m_locked = true;
    }
}

void ShobuNetwork::unlock()
{
    m_locked = false;
}

bool ShobuNetwork::createSocket(int port)
{
    int tmp_socket;

#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
        std::cout << "Could not initialize Winsock" << std::endl;
    }

#endif

    // Create the Socket
    tmp_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    // When an error on socket creation has occured
    if(tmp_socket < 0) {
        return false;
    }

    m_socket = tmp_socket;

    return true;
}

ShobuNetwork::~ShobuNetwork() {
    disconnect();
}




