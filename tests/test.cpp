#include <cstdio>
#include <thread>
#include "Network.h"

struct Game
{
    void setInput(int local, int remote) { }
    void update() { ++tick;}

    void storeState() {}
    void restoreState() {}

    int check() { return 0; }

    int tick;
};

// The callback used by the network library to update the game state
void networkGameUpdate(void* game_ptr, int local_input, int remote_input)
{
    Game& game = *((Game*)game_ptr);

    game.setInput(local_input, remote_input);

    printf("Tick: %d Local Input: %d Remote Input: %d\n", game.tick, local_input, remote_input);
    game.update();
}

// The callback used by the network library to store the game state
void networkStoreState(void* game_ptr)
{
    ((Game*)game_ptr)->storeState();
}

// The callback used by the network library to restore the game state
void networkRestoreState(void* game_ptr)
{
    ((Game*)game_ptr)->restoreState();
}

// The callback used by the network library to check for state desyncs
int networkCheckSync(void* game_ptr)
{
    return ((Game*)game_ptr)->check();
}


void RunHost(ShobuNetwork& network)
{
    network.initializeHost(8888);
    network.setInputDelay(3);

    network.waitForClient();

    while(network.connected()) {
        network.update(0);
        std::this_thread::sleep_for( std::chrono::milliseconds(500));
    }
}

void RunClient(ShobuNetwork& network)
{
    network.initializeClient("127.0.0.1", 8888);
    network.connectToHost();

    while(network.connected()) {
        network.update(1);
        std::this_thread::sleep_for( std::chrono::milliseconds(500));
    }
}

int main(int argc, char **argv)
{

    if(argc < 2) {
        printf("Usage: pass -c for running a client, pass -h for hosting.\n");
        return 0;
    }

    ShobuNetwork network;
    Game game;
    game.tick = 0;

    network.registerCallbacks(networkGameUpdate, networkStoreState, networkRestoreState, networkCheckSync, (void *)&game);

    if(argv[1][1] == 'c') {
        RunClient(network);
    } else if(argv[1][1] == 'h') {
        RunHost(network);
    }

    return 0;
}
