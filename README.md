# Shobu Network Library


## Usage


###Initialize the network
```
ShobuNetwork network;

// You must register required callbacks before establishing a connection with a remote client
network.registerCallbacks( gameUpdate, storeState, restoreState, checkSync, &user_data);

```

### If hosting
```

// Set the number of game updates to delay input
network.setInputDelay(3);

network.initializeHost(port);

// Returns when a client as connected or a time out occured
network.waitForClient();

if(network.connected()) {
   // Host specific initialization code here
   ...
}
```

### If connecting to the host

```
network.initializeClient(host_ip, host_port);

// Returns when connected to the host or timed out
network.connectToHost();

if(network.connected()) {
   // Client specific initialization code here
   ...
}
```

### After a connection is established you update your game like so
``` 
if(network.connected()) {
    network.update(local_input);
}
```

