### BTChat
btchat - primitive chat implemented over bluetooth l2cap

### Build
+ Requires Bluez dev libs: libbluetooth-dev
+ `cd btchat && make`

### Setup
+ `bluetoothctl` is the preferred configuration tool
+ Machines must both be discoverable to connect
    - `[bluetooth]# discoverable on` 
+ Client must have discovered server to connect
    - ```
      [bluetooth]# scan on
         ...
         [NEW] Device xx:xx:xx:xx:xx:xx Machine-A
         ...
         scan off
      ```
+ Trusting on both devices makes connection process smoother
    - `[bluetooth]# trust xx:xx:xx:xx:xx:xx`

### Usage
```
btchat [-c bdaddr] [-l] [-h handle] [-p psm]
    -c bdaddr  Connect to bdaddr (format: xx:xx:xx:xx:xx:xx)
    -l         Listen for connections
    -h handle  Set user handle
    -p psm     Set PSM in hex
```
1. Start a listening btchat process on Machine-A with: `btchat -l [options]`
    - Uses first available hci device on Machine-A
    - Machine-A will print the bdaddr it is listening on
2. Connect to Machine-A from Machine-B with `btchat -c [bdaddr] [options]`
3. Type something and hit enter
4. `ctrl + c`to close
