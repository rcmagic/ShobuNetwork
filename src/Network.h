#ifndef SHOBU_NETWORK_H
#define SHOBU_NETWORK_H

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <mutex>
#include <atomic>
#include <list>

const unsigned int MAX_INPUTS = 60;

class ShobuNetwork
{
    public:
    ShobuNetwork();
    ~ShobuNetwork();


    /*! Creates a new network host that waits for a connection
     * \param port The port on which to host a connection
     * \return false on failure, true on success
     */
    bool initializeHost(int port);

    /*! Creates a new network client that attempts to connect to a remote host
     * \param ip_addr
     * \param port
     * \return false on failure, true on success
     */
    bool initializeClient(const char* ip_addr, int port);


    // Handles updating the game state in network mode
    void update(int local_input);


    void waitForClient();
    void connectToHost();
    bool networkUpdate();

    void sendInput(int frame);
    void sendInput();

    void sendInputRequest();

    void addInputState(int state);

    bool hasInput(int frame);

    int getInput(int frame);
    int getLocalInput(int frame);

    void setRemoteInput(int input, int tick);
    void setLocalInput(int input, int tick);

    void setSynced() { m_remote_synced = true; }

    void setInputDelay(int delay);
    int getInputDelay() { return (int)m_delay; }

    void setRollbacks(bool value);

    void setLocalTick(int tick);
    int remoteTick();


    void disconnect();
    void disconnectWithoutMessage();

    bool connected();

    void sendDisconnect();

    void connectToMS(const char* key);

    int getLocalTick() { return m_local_tick; }
    int getRemoteTick() { return m_remote_tick; }

    void printBuffer();

    /*! Perform a single rollback after a single game update to check for desyncs
     * \param p1_input
     * \param p2_input
     * \return true when a desync was detected
     */
    bool testRollback(int p1_input, int p2_input);


    // Indicates if the current state is synced;
    bool stateIsSynced();

    // Override desync detection and set network's state to synced
    void forceSynced();

    // Stop game update and input syncing
    void stopSync();

    // Returns the current average with the remote client ping
    int getPing();


    /*!
     * \return true when this client the host
     */
    bool isHost();

    /*! Register required callback methods used by the network library to sync the game state
     * \param update a function which updates the game's state
     * \param store a function which stores the game's state
     * \param restore a function which restore the game's state
     * \param method that returns an integer used to check if the game's state is synced
     * \param data user data that is passed into each callback
     */
    void registerCallbacks(void (*update)(void*, int, int), void (*store)(void*), void (*restore)(void*), int (*sync)(void*), void* data);

    /*! Set packet loss frequency.
     * \param frequency chance of packet loss is 1/frequency
     */
    void setPacketLoss(int frequency);

    //! Sets packet delay before sending
    /*! Used to simulate network latency.
     * \param delay total frames to wait before sending a packet. Must be >= 0
     */
    void setPacketDelay(int delay);

    /*!  Wait on the other client to sync to the current tick
     *   then reset the current tick to 0
     */
    void wait();



    // Sends packets after a delay.  Used to test code during network latency
    void sendDelayedPackets();

    // Don't rollback when this is set
    std::atomic<bool> delayRollbacks;

    // True when trying to connect to a host or client
    std::atomic<bool> runHostThread;
    std::atomic<bool> runClientThread;
private:
    // Prevent the creation of copies of the ShobuNetwork instance
    //ShobuNetwork& operator= (const ShobuNetwork&) { return *this; }


    //ShobuNetwork(const ShobuNetwork&) {}

    // Handles returning to the common state of both clients and running inputs up to the current game tick
    void rollBack();

    // Check for game state divergence with the remote client
    void checkState(int state);

    /*! Used internally by ShobuNetwork to create a socket
     * \return false on failure, true on success
     */
    bool createSocket();

    void createInputBuffer(int p_size);


    // Sends out a response to a ping
    void sendPingResponse(unsigned int time_stamp);

    // Update current ping
    void updatePing(unsigned int time_stamp);


    void sendWaitCommand();

    int m_socket;  /// Socket which the connection is bound to

    struct sockaddr_in m_host_address;  /// Address of the remote host

    struct sockaddr_in m_remote_addr;  /// Address of client

    int m_input_buffer_size;

    unsigned char m_delay;  /// number of frames of input delay
    int m_sync;   /// the frame of the input buffer used for syncing (= m_input_buffer_size - m_delay - 1)

    std::atomic<bool> m_remote_synced; /// flag that keeps track of whether or not the clients are synced
    int m_remote_tick; /// Current tick of the remote game
    int m_local_tick;  /// Current tick of the local game
    int m_rollback_tick; /// Last known tick where the local and remote game states were in sync.  Used only if rollbacks are enabled

    bool m_connected;  /// flag that keeps track of the status of the remote connection

    char m_client; /// flag indicating client or server.  'c'=client, 's'=server

    std::mutex m_mutex; /// Used to lock when modifying buffers

    // Store at MAX_INPUTS inputs and loop to the front of the buffer when reaching the end
    int remote_buffer[MAX_INPUTS];
    int local_buffer[MAX_INPUTS];

    // A list of values for each game tick thats used to check for state divergence
    int m_check_buffer[MAX_INPUTS];

    // If set to true, rollbacks will be enabled
    bool m_rollbacks;

    // When > 0, simulate packet loss.  Packet loss is simulated at 100/m_testPackLoss %
    int m_testPacketLoss;

    // Used in testing network latency
    bool m_testNetworkLatency;
    int m_packetDelay;

    struct DelayedPacket {
        char* packet;
        int timer;
        std::size_t size;
    };

    std::list<DelayedPacket> m_packets;

    // Set to false when a state desynced is detected
    bool m_stateSynced;


    // Unique ordered packet id
    unsigned int m_packetId;

    // Id of the last packet recieved
    unsigned int m_lastPacketId;

    // When true hold game updates until the other client has synced up
    bool m_wait;

    // Other client is waiting
    bool m_remote_wait;


    // Current average packet round trip time
    int m_ping;

    // Keep track of the game tick difference between the client
    int m_tick_delta;

    // Update callback function
    void (*m_updateCallback)(void *data, int p1_input, int p2_input);

    // Store state callback function
    void (*m_storeCallback)(void *data);

    // Restore state callback function
    void (*m_restoreCallback)(void *data);

    // callback which returns an integer used to check if the game's state has diverged
    int (*m_syncCallback)(void *data);

    // user defined data passed to each callback
    void* m_userData;

};
#endif // SHOBU_NETWORK_H

