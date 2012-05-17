#include "Network.h"
#include <iostream>
#include <vector>
#include <boost/thread.hpp>

using namespace std;

int main()
{
    // Test the network library

    ShobuNetwork host;

    vector<int> test_buffer = { 1, 2, 3, 4, 5, 6, 7 };

    // Test setting up the host
    if(!host.initializeHost(8080)) {
        cerr << "Error initiailzing ShobuNetwork host" << endl;
        return 0;
    } else {
        cout << "Successfully initialized host"  << endl;
    }
    ShobuNetwork client;
    if(!client.initializeClient("127.0.0.1", 8080)) {
        cerr << "Error initiailzing ShobuNetwork host" << endl;
        return 0;
    } else {
        cout << "Successfully initialized client" << endl;
    }

    // Perform handshake
    boost::thread hostThread(&ShobuNetwork::waitForClient, &host);
    boost::thread clientThread(&ShobuNetwork::connectToHost, &client);

    hostThread.join();
    clientThread.join();

    boost::thread hostUpdateThread(&ShobuNetwork::networkUpdate, &host);
    boost::thread clientUpdateThread(&ShobuNetwork::networkUpdate, &client);

    // set host input buffer
    for(auto input : test_buffer) { host.addInputState(input);}

    // have the client send a request
    cout << "Client sending input request.." << endl;
    client.sendInputRequest();


    // Wait for request and send a frame
    hostUpdateThread.join();
    clientUpdateThread.join();



    return 0;


}
