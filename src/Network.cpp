#include "Network.h"
#include "NetworkLogger.h"

#include <chrono>
#include <iostream>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <fstream>
#include <thread>

struct NetworkMetric
{
    float waits;
    float update_rate;
    float rollbacks;
    float ping;

} static m_metrics { 0, 60, 0 };



static std::atomic<bool> saving_metrics(true);
void write_metrics()
{
    std::ofstream network_data("network_metrics.csv");
    network_data << "Update Rate, Waits / Sec, Rollbacks / Sec, Ping" << std::endl;
    while(saving_metrics) {
        // we write metrics once every second
        std::this_thread::sleep_for( std::chrono::seconds(1));

        network_data << m_metrics.update_rate << "," << m_metrics.waits << "," << m_metrics.rollbacks << "," << m_metrics.ping << std::endl;

        // reset metrics
        m_metrics.waits = 0;
        m_metrics.update_rate = 1600;
        m_metrics.rollbacks = 0;
    }


}


#define MAX_INPUT_DELAY 7

// Amount of inputs to store for rollbacks
const int OLD_FRAMES = 5;

// the MAX number of game ticks it is possible to roll back to
const int MAX_ROLLBACK = 15;

// How manytimes to send each packet to deal with packet lost
const int SEND_REPEATS = 2;

//static std::ofstream netlog;

// Thread use for listening to incoming network traffic
void listenThreadFunc(void* network)
{
    while(1)
    {
        if(((ShobuNetwork*)network)->networkUpdate()) {
            LogNull << "Terminated Network Thr  ead" << endline;
            return;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds(1));
    }
}

void packetDelayFunc(void* network)
{
    while(((ShobuNetwork*)network)->connected())
    {
        ((ShobuNetwork*)network)->sendDelayedPackets();
        std::this_thread::sleep_for( std::chrono::milliseconds(16));
    }
}
ShobuNetwork::ShobuNetwork()
{
    m_input_buffer_size = 0;


    // initialize local history of input buffer
    for(unsigned int i=0; i<MAX_INPUTS; i++) {
        local_buffer[i] = 0;
        remote_buffer[i] = 0;
        m_check_buffer[i] = 0;
    }

    m_connected = false;

    m_remote_tick = 0;
    m_local_tick = -1;
    m_rollback_tick = -1;

    m_remote_synced = true;

    m_rollbacks = true;

    m_delay = 2;

    m_stateSynced = true;


    // seed RNG for simulated packet loss
    srand(time(nullptr));


    // Setup variables for testing network latency
    m_testPacketLoss = 0;
    m_testNetworkLatency = false;
    m_packetDelay = 4;

    delayRollbacks = false;
//    netlog.open("net.log");

    // Start at 1 because we only check packets that have a higher id than one previous received.
    m_packetId = 1;

    m_lastPacketId = 0;

    m_wait = false;
    m_remote_wait = false;

    runHostThread = false;

    m_ping = 0;

    m_tick_delta = 0;


}

void ShobuNetwork::createInputBuffer(int p_size)
{
    m_input_buffer_size = p_size;
}

void ShobuNetwork::sendDelayedPackets()
{
    if(!m_testNetworkLatency) return;

    for(auto it = m_packets.begin(); it != m_packets.end(); it++) {
        if(it->timer > 0) {
            it->timer--;
        } else {
            for(int i=0; i<SEND_REPEATS; ++i) {
                sendto(m_socket,  it->packet, 128, 0, (struct sockaddr*)&m_remote_addr,
                                  sizeof(struct sockaddr));
            }
            delete [] it->packet;
            m_packets.erase(it--);
        }
    }
}

void ShobuNetwork::sendWaitCommand()
{
    char tmp_buffer[64];
    tmp_buffer[0] = 'w';
    tmp_buffer[1] = m_client;

    // Request the input we are missing since the last rollback
    memcpy(&tmp_buffer[2], &m_local_tick, 4);

    sendto(m_socket, tmp_buffer, 64, 0, (struct sockaddr*)&m_remote_addr,
           sizeof(struct sockaddr));
}

bool ShobuNetwork::initializeHost(int port)
{

    createSocket();

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

    m_client = 's';

    // Create thread for writing metrics
    std::thread(write_metrics).detach();

    return true;
}

bool ShobuNetwork::initializeClient(const char* ip_addr, int port)
{
    createSocket();

    // Initialize remote host address
    m_host_address.sin_family = PF_INET;
    m_host_address.sin_port = htons(port);
    m_host_address.sin_addr.s_addr = inet_addr(ip_addr);

    m_client = 'c';

    return true;
}

void ShobuNetwork::waitForClient()
{
    char tmp_buffer[2];
    tmp_buffer[0] = 'a';

    // Need to send the amount of input delay to use
    memcpy(&tmp_buffer[1], &m_delay, 1);

    struct sockaddr_in host_addr;
    socklen_t host_addr_size = sizeof(host_addr);
    char net_buffer[32];
    runHostThread = true;
    while(!m_connected && runHostThread) {
        int recv_bytes = recvfrom(m_socket, net_buffer, 32, 0, (struct sockaddr*)&host_addr, &host_addr_size);

        if(recv_bytes > 0) {
            switch(net_buffer[0]) {
            case 'c': // client requested a connection
                LogMessage << "Client connected. Input Delay is " << (unsigned int)m_delay << ". Sending handshake.." << endline;

                m_remote_addr = host_addr;
                // Send handshake
                for(int i=0; i<SEND_REPEATS; i++) {
                    sendto(m_socket, tmp_buffer, 2, 0, (sockaddr*)&m_remote_addr,
                                sizeof(struct sockaddr));
                }
                m_connected = true;

                // Start thread to listen to the client
                std::thread(listenThreadFunc, this).detach();
                if(m_testNetworkLatency) {
                    std::thread(packetDelayFunc, this).detach();
                }

                break;
            case 'p': // Whole punch testing
                LogNull << "Hole punch from server" << endline;
                break;
            default:
                LogNull << "Unknown packet received...: " <<  net_buffer << endline;
                break;
            }
        }
    }

    runHostThread = false;
}

void ShobuNetwork::sendDisconnect()
{
    char tmp_buffer[1];
    tmp_buffer[0] = 'd';
    sendto(m_socket, tmp_buffer, 1, 0, (struct sockaddr*)&m_remote_addr,
           sizeof(struct sockaddr));
}

void ShobuNetwork::printBuffer()
{
    LogNull << endline;
}
void ShobuNetwork::connectToHost()
{

    runClientThread = true;

    char tmp_buffer[1];
    tmp_buffer[0] = 'c';


    LogNull << "Sending handshake to the server" << endline;
    sendto(m_socket, tmp_buffer, 1, 0, (sockaddr*)&m_host_address,
                sizeof(struct sockaddr));

    // Set a timeout
    fd_set fds;
    struct timeval timeout;
    int rc;
    timeout.tv_sec = 4;
    timeout.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(m_socket, &fds);
    rc = select(sizeof(fds)*8, &fds, NULL, NULL, &timeout);
    if(rc ==-1) {
        LogNull << "Select error" << endline;
        return;
    } else if(rc >= 0) {
        if(!FD_ISSET(m_socket, &fds)) {
            LogNull << "Could not connect to host, Timed out" << endline;
            return;
        }
    } else {
        return;
    }

    struct sockaddr_in host_addr;
    socklen_t host_addr_size = sizeof(host_addr);
    char net_buffer[32];
    int recv_bytes = recvfrom(m_socket, net_buffer, 32, 0, (struct sockaddr*)&host_addr, &host_addr_size);

    if(recv_bytes < 2) {
        LogNull << "Packet size is too small." << endline;
        return;
    }

    switch(net_buffer[0]) {
    case 'a': // server sent handshake
        memcpy(&m_delay, &net_buffer[1], 1 );
        m_remote_addr = m_host_address;
        LogNull << "Received handshake from server. Input delay is " << (int)m_delay << endline;
        setInputDelay(m_delay);


        // Sleeping to avoid input carrying over
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        m_connected = true;

        // Start thread which listens to the remote host's packets
        std::thread(listenThreadFunc, this).detach();

        // Thread which handles simulated network latency
        if(m_testNetworkLatency) {
            std::thread(packetDelayFunc, this).detach();
        }

        break;
    default:
        break;
    }

    runClientThread = false;
}

bool ShobuNetwork::networkUpdate()
{

    std::unique_lock<std::mutex> lock(m_mutex);

    fd_set fds;
    struct timeval timeout;
    int rc;

    // set timeout
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(m_socket, &fds);
    rc = select(sizeof(fds)*8, &fds, NULL, NULL, &timeout);
    if(rc ==-1) {
        LogNull << "Select error" << endline;
        return true;
    } else if(rc > 0) {
        if(!FD_ISSET(m_socket, &fds)) {
            LogNull << "No sockets set..." << endline;
            return false;
        }
    } else {
        LogNull << "1 second without client packet." << endline;
        return false;
    }

    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof(remote_addr);
    char net_buffer[128];
    int new_remote_tick = 0;
    int state =0;

    unsigned int r_time_stamp = 0;

    unsigned int r_packet_id = 0;

    int r_tick_delta;

    // Wait for data from remote client
    int recv_bytes = recvfrom(m_socket, net_buffer, 128, 0, (struct sockaddr*)&remote_addr, &remote_addr_size);

    if(recv_bytes > 0) { // TODO make sure packet length is what we expect for each case!

        switch(net_buffer[0]) {
        case 'a':
            LogNull << "Received Handshake from server" << endline;
            break;
        case 'f':
            // Check to see if the remote game is not the server if this is the client, and vice versa
            if(net_buffer[1] != m_client && !delayRollbacks) {
                memcpy(&new_remote_tick, &net_buffer[2], 4);
                memcpy(&r_packet_id, &net_buffer[6+m_input_buffer_size*4+4], 4);


                memcpy(&r_time_stamp, &net_buffer[6+m_input_buffer_size*4+8], 4);

                // only store input buffer when get the packets in order
                if( (r_packet_id > m_lastPacketId) &&
                    (new_remote_tick > m_remote_tick) &&
                    (new_remote_tick <= m_rollback_tick + m_input_buffer_size) ) {

                    m_lastPacketId = r_packet_id;
                    m_remote_tick = new_remote_tick;
                    m_tick_delta = (m_local_tick - m_remote_tick);

                    // Copy remote inputs into the buffer
                    for(int i=0; i<m_input_buffer_size; i++) {
                        int input;
                        memcpy(&input, &net_buffer[6+i*4], 4);
                        setRemoteInput(input, i+m_remote_tick-m_sync);
                    }

                    memcpy(&state, &net_buffer[6+m_input_buffer_size*4], 4);


                    LogNull << "Got Tick: " << new_remote_tick << "\t LOCAL: " << m_local_tick
                               << "\t Input: " << getInput(m_remote_tick)
                               << "\t Packet ID: " << r_packet_id
                               << "\t Packet Length: " << recv_bytes
                               << endline;


                    // Get and check that the game states are synced
                    checkState(state);

                    // Compare local and remote deltas
                    memcpy(&r_tick_delta, &net_buffer[6+m_input_buffer_size*4+12], 4);

                    // Attempt to keep the client game ticks in sync with a 1 frame tolerence
                    //m_remote_synced = m_local_tick <= (m_remote_tick + m_delay+1);
                    m_remote_synced = r_tick_delta+1 >= m_tick_delta;
                    //LogMessage << "Remote Delta: " << r_tick_delta << "     Local Delta: " << m_local_tick - m_remote_tick << endline;

                } else if(new_remote_tick > m_remote_tick + m_input_buffer_size) {
                    LogNull << "Got Future Tick " << new_remote_tick << " , Old remote tick is " << m_remote_tick << endline;
                } else if(new_remote_tick != m_remote_tick) {
                    LogNull << "Got Old tick " << new_remote_tick << " , Current tick is " << m_remote_tick << endline;
                }
            }
            // m_remote_addr = remote_addr;


            sendPingResponse(r_time_stamp);
            break;
        case 'd':
            LogNull << "Remote client/host disconnected." << endline;
            disconnectWithoutMessage();
            break;
        case 'r':
            memcpy(&new_remote_tick, &net_buffer[2], 4);

            // Send the requested frame
            LogNull << "Received request for input for tick: " << new_remote_tick << endline;
            if(new_remote_tick > m_local_tick) {
                sendInput(m_local_tick);
            } else {
                sendInput(new_remote_tick);
            }
            break;
        case 'w': // Wait command
            memcpy(&new_remote_tick, &net_buffer[2], 4);
            LogNull << "Received wait command at tick: " << new_remote_tick << endline;

            // Only true when we're waiting too
            m_remote_wait = m_wait;
            break;

        case 'o': // Ping response
            memcpy(&r_time_stamp, &net_buffer[2], 4);
            updatePing(r_time_stamp);
            //LogMessage << "Got Ping Reply " << r_time_stamp << endline;
            break;
        default:
            LogMessage << "Got unknown network request " << (int)net_buffer[0] << endline;
            break;
        }
    } else if(recv_bytes == 0) {
        LogNull << "Socket was closed" << endline;
    } else {
        LogNull << "Socket error: " << strerror(errno) << endline;
        disconnect();

    }

    if(!m_connected) {
        return true;
    }
    return false;
}

void ShobuNetwork::sendInput()
{
    sendInput(m_local_tick);
}

void ShobuNetwork::sendInput(int frame)
{
    if(delayRollbacks) return;

    LogNull << "Sending Input: (" << frame << ")" << this->getLocalInput(m_local_tick)
               << "\t Packet Id: " << m_packetId
               << endline;


    char* tmp_buffer = new char[128];

    tmp_buffer[0] = 'f';
    tmp_buffer[1] = m_client;
    memcpy(&tmp_buffer[2], &frame, 4);
    for(unsigned int i=0; i<static_cast<unsigned int>(m_input_buffer_size); i++) {
        memcpy(&tmp_buffer[6+i*4], &local_buffer[(MAX_INPUTS+frame-m_input_buffer_size+1+m_delay+i) % MAX_INPUTS], 4);
    }

    // Add game state value used to test for syncing
    memcpy(&tmp_buffer[6+4*m_input_buffer_size], &m_check_buffer[(frame-MAX_ROLLBACK+MAX_INPUTS) % MAX_INPUTS], 4);


    // Add packet id
    memcpy(&tmp_buffer[6+4*m_input_buffer_size+4], &m_packetId, 4);

    ++m_packetId;

    // Add time stamp
    unsigned int time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    memcpy(&tmp_buffer[6+4*m_input_buffer_size+8], &time_stamp, 4);

    // Send tick delta
    memcpy(&tmp_buffer[6+4*m_input_buffer_size+12], &m_tick_delta, 4);

    // Don't send packets when testing for latency right now.  They are sent later
    if(m_testNetworkLatency) {
        DelayedPacket packet = {tmp_buffer, m_packetDelay, 128 };
        m_packets.push_back(packet);
    }

    // Simulate packet loss
    if(m_testPacketLoss > 0 && (rand()%m_testPacketLoss)==0) {
        return;
    }

    if(!m_testNetworkLatency) {
        for(int i=0; i<SEND_REPEATS; i++) {
            sendto(m_socket, tmp_buffer, 128, 0, (struct sockaddr*)&m_remote_addr,
                              sizeof(struct sockaddr));
        }
        delete [] tmp_buffer;
    }
}


void ShobuNetwork::sendPingResponse(unsigned int time_stamp)
{

    char* tmp_buffer = new char[32];

    tmp_buffer[0] = 'o';
    tmp_buffer[1] = m_client;
    memcpy(&tmp_buffer[2], &time_stamp, 4);

    // Don't send packets when testing for latency right now.  They are sent later
    if(m_testNetworkLatency) {
        DelayedPacket packet = {tmp_buffer, m_packetDelay, 32 };
        m_packets.push_back(packet);
    }

    // Simulate packet loss
    if(m_testPacketLoss > 0 && (rand()%m_testPacketLoss)==0) {
        return;
    }

    if(!m_testNetworkLatency) {
        for(int i=0; i<SEND_REPEATS; i++) {
            sendto(m_socket, tmp_buffer, 32, 0, (struct sockaddr*)&m_remote_addr,
                              sizeof(struct sockaddr));
        }
        delete [] tmp_buffer;
    }
}

void ShobuNetwork::updatePing(unsigned int time_stamp)
{
    unsigned int diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - time_stamp;

    // Weight the ping average towards the existing ping value
    m_ping = m_ping*0.90+0.10*diff;
}

int ShobuNetwork::getPing()
{
    return m_ping;
}

void ShobuNetwork::connectToMS(const char* key)
{
    createSocket();

    struct sockaddr_in host_address;
    // Initialize remote host address
    host_address.sin_family = PF_INET;
    host_address.sin_port = htons(60417);
    host_address.sin_addr.s_addr = inet_addr("toasty-walrus-shobu.rhcloud.com");

    sendto(m_socket, key, strlen(key), 0, (struct sockaddr*)&host_address,
                      sizeof(struct sockaddr));
}

void ShobuNetwork::sendInputRequest()
{
    char tmp_buffer[64];
    tmp_buffer[0] = 'r';
    tmp_buffer[1] = m_client;

    // Request the input we are missing since the last rollback
    int request_tick = m_rollback_tick+m_input_buffer_size;
    memcpy(&tmp_buffer[2], &request_tick, 4);

    sendto(m_socket, tmp_buffer, 64, 0, (struct sockaddr*)&m_remote_addr,
           sizeof(struct sockaddr));
}

void ShobuNetwork::addInputState(int state)
{
    setLocalInput(state, m_local_tick+m_delay);
}

bool ShobuNetwork::hasInput(int frame)
{
    return m_remote_tick+m_delay-1 >= frame;
}

int ShobuNetwork::getInput(int frame)
{
    int offset = (MAX_INPUTS + frame) % MAX_INPUTS;
    return remote_buffer[offset];
}

int ShobuNetwork::getLocalInput(int frame)
{
    int offset = (MAX_INPUTS + frame) % MAX_INPUTS;
    return local_buffer[offset];
}

void ShobuNetwork::setRemoteInput(int input, int tick) 
{
    if(tick == 0) {
        LogNull << "ASDF: " << input << endline;
    }
    remote_buffer[(MAX_INPUTS+tick)%MAX_INPUTS] = input;
}

void ShobuNetwork::setLocalInput(int input, int tick) 
{
    local_buffer[(MAX_INPUTS+tick)%MAX_INPUTS] = input;
}

void ShobuNetwork::setInputDelay(int delay)
{
    if(delay > MAX_INPUT_DELAY) {
        delay = MAX_INPUT_DELAY;
    }
    createInputBuffer(OLD_FRAMES+2*delay);

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
        sendDisconnect();
#ifdef WIN32
         WSACleanup( );
#else
        close(m_socket);
#endif
        LogNull << "Last local tick " << m_local_tick <<  ", Rollback " << m_rollback_tick << endline;
        m_connected = false;
    }
}


void ShobuNetwork::disconnectWithoutMessage()
{
    if(m_connected) {
#ifdef WIN32
         WSACleanup( );
#else
        close(m_socket);
#endif
        LogNull << "Last local tick " << m_local_tick << endline;

        m_connected = false;
    }
}
bool ShobuNetwork::connected()
{
    return m_connected;
}

bool ShobuNetwork::createSocket()
{
    int tmp_socket;

#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) {
        LogNull << "Could not initialize Winsock" << endline;
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

void ShobuNetwork::rollBack()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // Return the game to the last common state    
    m_restoreCallback(m_userData);

    // decide up to what game tick to advance to in which the clients maintain a common state
    int min_tick = m_local_tick < m_remote_tick + m_delay? m_local_tick : m_remote_tick + m_delay;

    int frame=m_rollback_tick+1;
    for(; frame <= min_tick; frame++) {

        m_updateCallback(m_userData, local_buffer[(frame + MAX_INPUTS) % MAX_INPUTS ],
                        remote_buffer[(frame + MAX_INPUTS) % MAX_INPUTS]);

        // Get value for state divergence checking
        m_check_buffer[frame % MAX_INPUTS] = m_syncCallback(m_userData);

//        if(m_client == 's') {
//            LogMessage << frame << " " <<  local_buffer[(frame + MAX_INPUTS) % MAX_INPUTS ] << " " << remote_buffer[(frame + MAX_INPUTS) % MAX_INPUTS] << " " << m_check_buffer[frame % MAX_INPUTS] << endline;
//        } else {
//            LogMessage << frame << " " << remote_buffer[(frame + MAX_INPUTS) % MAX_INPUTS] << " " <<  local_buffer[(frame + MAX_INPUTS) % MAX_INPUTS ] << " " << m_check_buffer[frame % MAX_INPUTS] << endline;
//        }
    }

    // Keep track of the last game tick we stored the state at.
    m_rollback_tick = min_tick;

    // Both clients should now be in sync, so store the state
    m_storeCallback(m_userData);


    // Process the rest of the input up to the current tick
    for(; frame<=m_local_tick; frame++) {
        // Repeat the last state for the remote buffer as simple form of input prediction.
        m_updateCallback(m_userData, local_buffer[(frame + MAX_INPUTS) % MAX_INPUTS ],
                        remote_buffer[(m_rollback_tick + MAX_INPUTS) % MAX_INPUTS ]);
    }

}



const float avg_weight = 0.90f;
static auto last_time = std::chrono::high_resolution_clock::now();

void update_metrics()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto dur = now - last_time;

    float diff = std::chrono::duration_cast<std::chrono::microseconds>(dur).count();

    m_metrics.update_rate = m_metrics.update_rate*avg_weight+(1.0f-avg_weight)*diff;

    last_time = now;
}

void ShobuNetwork::update(int local_input)
{


    // Wait on the other client to catch up to the current tick before continuing
    // This is usually set while waiting for the start of a match after loading
    if(m_wait) {

        std::unique_lock<std::mutex> lock(m_mutex);

        // Tell the other client this game is waiting
        sendWaitCommand();

        // Doesn't update the game until we know the remote game has caught up
        if(!m_remote_wait) {
            return;
        }

        m_remote_wait = false;
        m_wait = false;
        m_remote_tick = 0;
        m_local_tick = -1;
        m_rollback_tick = -1;

        m_remote_synced = true;

        m_stateSynced = true;

        // Clear input buffers
        for(unsigned int i=0; i<MAX_INPUTS; i++) {
            local_buffer[i] = 0;
            remote_buffer[i] = 0;
            m_check_buffer[i] = 0;
        }

    }

    // Used for recording network metrics
    update_metrics();
    m_metrics.ping = m_ping;

    // If we are desynced and we have the inputs from the remote client to resync, rollback
    if(!delayRollbacks && m_rollbacks && m_local_tick > m_rollback_tick && hasInput(m_rollback_tick+1) ) {
        rollBack();

        // record how many rollbacks occured
        ++m_metrics.rollbacks;
    }

    int next_local = 0;
    int next_remote = 0;

    // Want to stay close to the other client's game tick so we don't drift out of sync. 
    // So we sometimes wait a cycle to let the other client catch up

    if(delayRollbacks) {
        // Update with no input.  Typicall this is used while loading
        m_updateCallback(m_userData, 0, 0);
    } else if(((m_rollbacks && m_remote_synced && m_local_tick < m_rollback_tick + MAX_ROLLBACK)
            || (!m_rollbacks && m_remote_synced && hasInput(m_local_tick+1)))) {

        m_local_tick++;

        // Add the local player's new input to the buffer
        addInputState(local_input);

        // Update input state for the current frame with what's stored in the local and remote input buffers
        next_local = getLocalInput(m_local_tick);
        next_remote = hasInput(m_local_tick) ? getInput(m_local_tick) : getInput(m_remote_tick+m_delay);

        // Update the game state
        m_updateCallback(m_userData, next_local, next_remote);

        // If the last frame was synced and we have input for this frame, we are still synced, so store game state
        if(m_local_tick == (m_rollback_tick + 1) && hasInput(m_local_tick) && !delayRollbacks) {
            m_rollback_tick++;
            m_storeCallback(m_userData);

            // Get value for state divergence checking
            m_check_buffer[(m_rollback_tick+MAX_INPUTS) % MAX_INPUTS] = m_syncCallback(m_userData);

//                if(m_client == 's') {
//                    LogMessage << m_rollback_tick << " " << next_local << " " << next_remote << " " << m_syncCallback(m_userData) << endline;
//                } else {
//                    LogMessage << m_rollback_tick << " " << next_remote << " " << next_local << " " << m_syncCallback(m_userData) << endline;
//                }

        }
    } else {
        // Only skip 1 frame when out of sync to let the remote client catch up
        m_remote_synced = true;

        // Might as well request input from the remote client while waiting
        sendInputRequest();

        // Increment waiting count for metrics
        ++m_metrics.waits;
    }

    // Send updated input buffer to the remote client
    sendInput();

}

bool ShobuNetwork::testRollback(int p1_input, int p2_input)
{
    // Store the current game state
    m_storeCallback(m_userData);

    // Run the game update
    m_updateCallback(m_userData, p1_input, p2_input);

    // Get value to check for a state desync
    int sync_check = m_syncCallback(m_userData);

    // Return to previous state
    m_restoreCallback(m_userData);

    // Run update again to return to the current state
    m_updateCallback(m_userData, p1_input, p2_input);

    // Test if the state diverged
    return sync_check != m_syncCallback(m_userData);
}

void ShobuNetwork::checkState(int state)
{
    // We check the state more than MAX_ROLLBACK ticks ago to be sure both clients have processed inputs for it.
    if(m_check_buffer[(m_remote_tick-MAX_ROLLBACK+MAX_INPUTS)%MAX_INPUTS] != state) {
        LogMessage << "Desync:" << m_remote_tick-MAX_ROLLBACK << "  " << local_buffer[(m_remote_tick-MAX_ROLLBACK+MAX_INPUTS-1)%MAX_INPUTS] << "   "
                  << remote_buffer[(m_remote_tick-MAX_ROLLBACK+MAX_INPUTS-1)%MAX_INPUTS] << endline;
        m_stateSynced = false;

    }
}

bool ShobuNetwork::stateIsSynced()
{
    return m_stateSynced;
}

void ShobuNetwork::forceSynced()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    LogNull << "Forcing sync" << endline;
    delayRollbacks = false;

}

void ShobuNetwork::stopSync()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    LogNull << "Stopping sync" << endline;
    m_remote_tick = 0;
    m_local_tick = -1;
    m_rollback_tick = -1;

    m_remote_synced = true;

    m_stateSynced = true;
    delayRollbacks = true;

    // Clear input buffers
    for(unsigned int i=0; i<MAX_INPUTS; i++) {
        local_buffer[i] = 0;
        remote_buffer[i] = 0;
        m_check_buffer[i] = 0;
    }

}

void ShobuNetwork::setPacketLoss(int frequency)
{
    if(frequency >= 0) {
        m_testPacketLoss = frequency;
    }
}

void ShobuNetwork::setPacketDelay(int delay)
{
    if(delay > 0) {
        m_packetDelay = delay;
        m_testNetworkLatency = true;
    } else {
        m_packetDelay = 0;
        m_testNetworkLatency = false;
    }
}

void ShobuNetwork::setRollbacks(bool value)
{
    if(value) {
        LogMessage << "Network rollbacks enabled" << endline;
    } else {
        LogMessage << "Network rollbacks disabled" << endline;
    }

    m_rollbacks = value;
}

void ShobuNetwork::wait()
{
    m_wait = true;
}

bool ShobuNetwork::isHost()
{
    return m_client == 's';
}

void ShobuNetwork::registerCallbacks(void (*update)(void *, int, int), void (*store)(void *), void (*restore)(void *), int (*sync)(void*), void *data)
{
    m_updateCallback = update;
    m_storeCallback = store;
    m_restoreCallback = restore;
    m_syncCallback = sync;
    m_userData = data;
}

ShobuNetwork::~ShobuNetwork() {
    disconnect();

    // stop metrics thread
    saving_metrics = false;
}




