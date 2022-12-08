# MCTP Wrapper C++ Library

MCTP wrapper library is introduced to make the life of developers easy.
People who write applications that uses MCTP layer for communication will
have to discover and talk to mctp endpoints. Each endpoint will be 
identified by an integer identifier called endpoint id or EID.
These applications can be PLDM daemon or NVMe MI daemon etc.
In some environments mctp stack may be implemented as
a service which exposes its APIs over DBus. On some machines it may be
implemented directly in kernel. Each application developer can seperate MCTP
communication to another layer to accomodate this changes. But a library
makes things much easier.

For example the APIs provided by this library allow user to
* Discover EIDs
* Send request and receive response
* Listen for EIDs added and removed dynamically

So the implementation is not a concern to the developer. Developer can focus
on the application logic and implement it.

## Building
This library uses meson as build system. The build is tested only on Ubuntu
 18.04.

Execute this command to create a build subdirectory and setup meson
```
meson setup build -Dexamples=enabled
```
This will fetch and build prequisites if needed including boost sdbusplus
 etc. Then make the library using
```
meson compile -C build -v
```
## Library variants
There are two variants for mctpwplus library. One built with -DBOOST_ASIO_DISABLE_THREADS flag
and one without it. The output names are libmctpwlus-nothread.so and libmctpwplus.so
respectively. **If your application is compiled with -DBOOST_ASIO_DISABLE_THREADS and if
you link with libmctpwplus.so then application can crash**
User need to choose -lmctpwplus or -lmctpwplus-nothread based on usage.

## Example
The main class provided by mctpwplus is MCTPWrapper. The object of this
class can be used for all MCTP communication purposes. MCTPWrapper class
takes one MCTPConfiguration object in constructor. The configuration object
specifies what is the message type and which binding type to use.
 ```cpp
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);
 ```
Then the mctpWrapper object can be used to discover and talk to EIDs. 

**Note that MCTPWrapper can talk to EIDs only after they are detected using
detectMctpEndpoints() method. Means any send receive api can be used only after
detectMctpEndpoints() is called.**
```cpp
// To detect available endpoints
mctpWrapper.detectMctpEndpoints(yield);
// ep will have all available EIDs in the system
auto& ep = mctpWrapper.getEndpointMap();

// Send request to EID and receive response
std::vector<uint8_t> request2 = {1, 143, 0, 3, 0, 0, 0, 0, 1, 0};
auto rcvStatus = mctpWrapper.sendReceiveYield(
    yield, eid, request2, std::chrono::milliseconds(100));
```
There are more examples available in examples directory which deal with more
specific functions.

## Public APIs

### MCTPConfiguration

MCTPConfiguration objects can define parameters like MCTP message type and
binding type. MCTPWrapper constructor expects an MCTPConfiguration object. So
configuration object must be created before creating wrapper object.
 ```cpp
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
 ```
Using the above config object to create wrapper will.
* Filter MCTP services with binding type SMBus
* Inside those services only EIDs which supports message type PLDM will be added
to endpoint list.

There is also option to use VendorId filtering if binding type is PCIe.

Refer examples/wrapper_object.cpp for sample code

### Constructor
MCTPWrapper class defines 2 types of constructors. One variant takes boost
io_context and other one takes shared_ptr to boost asio connection. Internally
MCTPWrapper needs a sdbusplus connection object to work. If io_context is
passed then a new connection object will be created. Or an existing connection
object can be shared also.<br>
Example 1.
```cpp
    boost::asio::io_context io;
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(io, config, nullptr, nullptr);
 ```
Example 2.
```cpp
    boost::asio::io_context io;
    auto connection = std::make_shared<sdbusplus::asio::connection>(io);
    MCTPConfiguration config(mctpw::MessageType::pldm,
                             mctpw::BindingType::mctpOverSmBus);
    MCTPWrapper mctpWrapper(connection, config, nullptr, nullptr);
 ```
Then the mctpWrapper object can be used to discover and talk to EIDs. Refer examples/wrapper_object.cpp for sample code

### DetectMctpEndpoints
It also has two variants. Async and yield based.
```cpp
void detectMctpEndpointsAsync(StatusCallback&& callback);
boost::system::error_code
        detectMctpEndpoints(boost::asio::yield_context yield);
```
This API must be called before accessing any send receive functions. This API
will scan the system for available MCTP services. Detect end points inside
them. Filter them based on given message type. And populate endpoint list.
MCTPWrapper will know how to send payload to an EID only after this API is
called. 
Refer examples/scan_endpoints.cpp for sample code
```cpp
boost::asio::spawn(io,
[&mctpWrapper](boost::asio::yield_context yield) {
    auto ec = mctpWrapper.detectMctpEndpoints(yield);
    auto epMap = mctpWrapper.getEndpointMap();
    for (const auto& [eid, serviceName] : epMap)
    {
        std::cout << "Eid " << static_cast<int>(eid) << " on "
                << serviceName.second << '\n';
    }
});
```
```cpp
auto registerCB = [](boost::system::error_code ec,
                                        void* ctx) {
    if (ec)
    {
        std::cout << "Error: " << ec << std::endl;
        return;
    }
    if (ctx)
    {
        auto wrapper = reinterpret_cast<MCTPWrapper*>(ctx);
        auto epMap = wrapper->getEndpointMap();
        for (const auto& [eid, serviceName] : epMap)
        {
            std::cout << "Eid " << static_cast<int>(eid) << " on "
                    << serviceName.second << '\n';
        }
    }
};
mctpWrapper.detectMctpEndpointsAsync(registerCB);
```
### SendReceive API
**Note that MCTPWrapper can talk to EIDs only after they are detected using
detectMctpEndpoints() method. Means any send receive api can be used only after
detectMctpEndpoints() is called.** Refer examples/send_receive.cpp and examples/receive_callback.cpp for sample code
```cpp
void sendReceiveAsync(ReceiveCallback receiveCb, eid_t dstEId,
                          const ByteArray& request,
                          std::chrono::milliseconds timeout);
std::pair<boost::system::error_code, ByteArray>
        sendReceiveYield(boost::asio::yield_context yield, eid_t dstEId,
                         const ByteArray& request,
                         std::chrono::milliseconds timeout);
```
SendReceive APIs can be used after detectMctpEndpoints is called. It also has yield and async variant.<br>
Async Example
```cpp
auto recvCB = [](boost::system::error_code err,
                     const std::vector<uint8_t>& response) {
    if (err)
    {
        // Error
    }
    else
    {
        // Valid response
    }
};
std::vector<uint8_t> request = {1, 143, 0, 3, 0, 0, 0, 0, 1, 0};
mctpWrapper.sendReceiveAsync(recvCB, eid, request,
                                     std::chrono::milliseconds(100));
```
Yield Example
```cpp
std::vector<uint8_t> request2 = {1, 143, 0, 3, 0, 0, 0, 0, 1, 0};
auto rcvStatus = mctpWrapper.sendReceiveYield(
    yield, eid, request2, std::chrono::milliseconds(100));
if (rcvStatus.first)
{
    std::cout << "Yield Error " << rcvStatus.first.message();
}
else
{
    // Valid response
}
```
MCTP stack uses message tag to identify request and matching response. 
Sometimes MCTP stack receive messages where matching message tag is not present.
For example a request message generated by an endpoint device.
MCTPWrapper allows user to register a callback which will be executed when
such an MCTP message is received in transport. This is done by passing a
ReceiveMessageCallback object as a parameter in the constructor. Example:
```cpp
auto onMCTPReceive = [](void*, eid_t eidReceived, bool, uint8_t,
                        const std::vector<uint8_t>& data, int status) {
    std::cout << "EID " << static_cast<int>(eidReceived)
                << '\n';
    std::cout << "Response ";
    for (int n : data)
    {
        std::cout << n << ' ';
    }
    std::cout << '\n';
};
MCTPWrapper mctpWrapper(io, config, nullptr, onMCTPReceive);
```
The arguments with which the callback is invoked are

* void* context - Pointer to MCTPWrapper object
* eid_t srcEid - EID which generated the message
* bool tagOwner - Value of tag owner bit in the message. True means request.
* uint8_t msgTag - 3 bit value to track the message
* const std::vector<uint8_t>& payload - MCTP payload bytes
* int status - Status of the callback operation. 0 means success


### Detecting devices added or removed during runtime
MCTPWrapper allows user to register a callback to be invoked whenever a change
 in network happens. For example a device is removed from the network. This
 is done by providing a ReconfigurationCallback object in constructor. One
 example usecase can be an NVMe disk management daemon. It can register for 
 callback and whenever a new NVMe drive is plugged MCTP stack can detect the
 event and it will assign an eid to it and then NVMe daemon can be notified
 through this callback. It can then include the new drive also in managed
 device list.

 Example
```cpp
static void onDeviceUpdate(void*, const Event& evt, boost::asio::yield_context&)
{
    switch (evt.type)
    {
        case Event::EventType::deviceAdded: {
            std::cout << "Device added " << static_cast<int>(evt.eid) << '\n';
        }
        break;
        case Event::EventType::deviceRemoved: {
            std::cout << "Device removed " << static_cast<int>(evt.eid) << '\n';
        }
        break;
    }
}

MCTPWrapper mctpWrapper(io, config, onDeviceUpdate, nullptr);
```
The structure `Event` has information like if the device is added or removed and its EID. The first argument of the callback will be a pointer to the MCTPWrapper object itself.
