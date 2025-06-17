// -------------------------------------------------------------------------------------------------------------
// NetworkManager.cpp - Implementation of network communication system
// Provides comprehensive TCP/UDP networking with authentication and command processing
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"

#if defined(__USE_NETWORKING__)
#include "NetworkManager.h"
#include "ThreadLockHelper.h"

// Constructor - Initialize all member variables to safe defaults
NetworkManager::NetworkManager() :
    m_isInitialized(false),                                             // Network subsystem not yet initialized
    m_isCleanedUp(false),                                               // Cleanup not yet performed
    m_lastAuthResult(AuthResult::NETWORK_ERROR),                        // Default to network error state
    m_networkThreadRunning(false),                                      // Network thread not running
    m_connectionTimeoutMs(10000),                                       // 10 second connection timeout
    m_pingIntervalMs(10000),                                            // 10 second ping interval
    m_maxRetryAttempts(3),                                              // Maximum 3 retry attempts per packet
    m_nextSequenceNumber(1),                                            // Start sequence numbers at 1
    m_expectedPacketID(1)                                               // Expect first packet ID to be 1
{
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager constructor called");
    #endif

    // Initialize connection structure to safe defaults
    m_connection.socket = INVALID_SOCKET;                               // No valid socket initially
    m_connection.protocol = NetworkProtocol::TCP;                       // Default to TCP protocol
    m_connection.state = ConnectionState::DISCONNECTED;                 // Start in disconnected state
    m_connection.serverPort = 0;                                        // No server port set
    m_connection.packetsReceived = 0;                                   // Zero packets received initially
    m_connection.packetsSent = 0;                                       // Zero packets sent initially

    // Register default command handlers for essential network operations
    RegisterCommandHandler(NetworkCommand::CMD_LOGIN_RESPONSE,
        [this](const NetworkPacket& packet) { HandleLoginResponse(packet); });
    RegisterCommandHandler(NetworkCommand::CMD_LOGOUT_RESPONSE,
        [this](const NetworkPacket& packet) { HandleLogoutResponse(packet); });
    RegisterCommandHandler(NetworkCommand::CMD_PING,
        [this](const NetworkPacket& packet) { HandlePingCommand(packet); });
    RegisterCommandHandler(NetworkCommand::CMD_PONG,
        [this](const NetworkPacket& packet) { HandlePong(packet); });
    RegisterCommandHandler(NetworkCommand::CMD_ERROR,
        [this](const NetworkPacket& packet) { HandleError(packet); });

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager default command handlers registered");
    #endif
}

// Destructor - Ensure proper cleanup of all network resources
NetworkManager::~NetworkManager() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager destructor called");
    #endif

    // Perform cleanup if not already done
    if (!m_isCleanedUp) {
        Cleanup();
    }
}

// Initialize the network subsystem and prepare for connections
bool NetworkManager::Initialize() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager::Initialize() called");
    #endif

    // Prevent double initialization
    if (m_isInitialized) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"NetworkManager already initialized");
        #endif
        return true;
    }

    // Initialize Windows Sockets subsystem
    if (!InitializeWinsock()) {
        SetLastError("Failed to initialize Winsock");
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize Winsock subsystem");
        #endif
        return false;
    }

    // Reset all statistics to zero
    ResetStatistics();

    // Clear any previous error messages
    ClearLastError();

    // Mark as successfully initialized
    m_isInitialized = true;

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager successfully initialized");
    #endif

    return true;
}

// Clean up all network resources and shutdown connections
void NetworkManager::Cleanup() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager::Cleanup() called");
    #endif

    // Prevent double cleanup
    if (m_isCleanedUp) {
        return;
    }

    // Stop network thread if running
    if (m_networkThreadRunning.load()) {
        StopNetworkThread();
    }

    // Disconnect from server if connected
    if (IsConnected()) {
        DisconnectFromServer();
    }

    // Close socket if open
    if (m_connection.socket != INVALID_SOCKET) {
        closesocket(m_connection.socket);
        m_connection.socket = INVALID_SOCKET;
    }

    // Clear all packet queues
    {
        while (!m_incomingPackets.empty()) {
            m_incomingPackets.pop();
        }
        while (!m_outgoingPackets.empty()) {
            m_outgoingPackets.pop();
        }
    }

    // Remove any active network locks
    threadManager.RemoveLock(LOCK_PACKET_QUEUE);
    threadManager.RemoveLock(LOCK_CONNECTION_STATE);

    // Cleanup Winsock
    CleanupWinsock();

    // Reset initialization state
    m_isInitialized = false;
    m_isCleanedUp = true;

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager cleanup completed");
    #endif
}

// Establish connection to game server
bool NetworkManager::ConnectToServer(const std::string& serverAddress, uint16_t port, NetworkProtocol protocol) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Attempting to connect to server: %S:%d",
            serverAddress.c_str(), port);
    #endif

    // Ensure network subsystem is initialized
    if (!m_isInitialized) {
        SetLastError("NetworkManager not initialized");
        return false;
    }

    // Disconnect from any existing connection
    if (IsConnected()) {
        DisconnectFromServer();
    }

    // Update connection state
    UpdateConnectionState(ConnectionState::CONNECTING);

    // Store connection parameters
    m_connection.serverAddress = serverAddress;
    m_connection.serverPort = port;
    m_connection.protocol = protocol;

    // Create appropriate socket for protocol
    m_connection.socket = CreateSocket(protocol);
    if (m_connection.socket == INVALID_SOCKET) {
        SetLastError("Failed to create socket");
        UpdateConnectionState(ConnectionState::ERROR_STATE);
        return false;
    }

    // Attempt to connect to server
    if (!ConnectSocket(m_connection.socket, serverAddress, port)) {
        SetLastError("Failed to connect to server");
        closesocket(m_connection.socket);
        m_connection.socket = INVALID_SOCKET;
        UpdateConnectionState(ConnectionState::ERROR_STATE);
        return false;
    }

    // Store connection establishment time
    m_connection.connectTime = std::chrono::steady_clock::now();

    // Update connection state to connected
    UpdateConnectionState(ConnectionState::CONNECTED);

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully connected to server");
    #endif

    return true;
}

// Gracefully disconnect from the server
void NetworkManager::DisconnectFromServer() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Disconnecting from server");
    #endif

    // Send graceful disconnect command if connected
    if (m_connection.state == ConnectionState::CONNECTED ||
        m_connection.state == ConnectionState::AUTHENTICATED) {
        SendPacket(NetworkCommand::CMD_DISCONNECT);
    }

    // Close socket connection
    if (m_connection.socket != INVALID_SOCKET) {
        closesocket(m_connection.socket);
        m_connection.socket = INVALID_SOCKET;
    }

    // Clear user authentication
    m_currentUser = UserCredentials();
    m_lastAuthResult = AuthResult::NETWORK_ERROR;

    // Update connection state
    UpdateConnectionState(ConnectionState::DISCONNECTED);

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Disconnected from server");
    #endif
}

// Check if currently connected to server
bool NetworkManager::IsConnected() const {
    ThreadLockHelper connectionLock(threadManager, LOCK_CONNECTION_STATE, 1000);
    if (!connectionLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire connection lock in IsConnected()");
        #endif
        return false;
    }

    return (m_connection.state == ConnectionState::CONNECTED ||
        m_connection.state == ConnectionState::AUTHENTICATED ||
        m_connection.state == ConnectionState::AUTHENTICATING);
}

// Get current connection state
ConnectionState NetworkManager::GetConnectionState() const {
    ThreadLockHelper connectionLock(threadManager, LOCK_CONNECTION_STATE, 1000);
    if (!connectionLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire connection lock in GetConnectionState()");
        #endif
        return ConnectionState::ERROR_STATE;
    }

    return m_connection.state;
}

// Authenticate user with server
bool NetworkManager::AuthenticateUser(const std::string& username, const std::string& password) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Authenticating user: %S", username.c_str());
    #endif

    // Must be connected to server first
    if (!IsConnected()) {
        SetLastError("Not connected to server");
        m_lastAuthResult = AuthResult::NETWORK_ERROR;
        return false;
    }

    // Update connection state to authenticating
    UpdateConnectionState(ConnectionState::AUTHENTICATING);

    // Store user credentials
    m_currentUser.username = username;
    m_currentUser.password = password;  // Note: In production, this should be hashed
    m_currentUser.lastActivity = std::chrono::steady_clock::now();

    // Create authentication packet data
    std::vector<uint8_t> authData;

    // Add username length and data
    uint32_t usernameLength = static_cast<uint32_t>(username.length());
    authData.insert(authData.end(), reinterpret_cast<uint8_t*>(&usernameLength),
        reinterpret_cast<uint8_t*>(&usernameLength) + sizeof(usernameLength));
    authData.insert(authData.end(), username.begin(), username.end());

    // Add password length and data
    uint32_t passwordLength = static_cast<uint32_t>(password.length());
    authData.insert(authData.end(), reinterpret_cast<uint8_t*>(&passwordLength),
        reinterpret_cast<uint8_t*>(&passwordLength) + sizeof(passwordLength));
    authData.insert(authData.end(), password.begin(), password.end());

    // Send authentication request to server
    if (!SendPacket(NetworkCommand::CMD_LOGIN_REQUEST, authData)) {
        SetLastError("Failed to send authentication request");
        m_lastAuthResult = AuthResult::NETWORK_ERROR;
        UpdateConnectionState(ConnectionState::ERROR_STATE);
        return false;
    }

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Authentication request sent to server");
    #endif

    return true;
}

// Logout current user
bool NetworkManager::LogoutUser() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Logging out current user");
    #endif

    // Must be authenticated to logout
    if (!IsUserAuthenticated()) {
        SetLastError("No user currently authenticated");
        return false;
    }

    // Send logout request to server
    if (!SendPacket(NetworkCommand::CMD_LOGOUT_REQUEST)) {
        SetLastError("Failed to send logout request");
        return false;
    }

    // Clear user credentials
    m_currentUser = UserCredentials();

    // Update connection state
    UpdateConnectionState(ConnectionState::CONNECTED);

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"User logout completed");
    #endif

    return true;
}

// Check if user is currently authenticated
bool NetworkManager::IsUserAuthenticated() const {
    ThreadLockHelper connectionLock(threadManager, LOCK_CONNECTION_STATE, 1000);
    if (!connectionLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire connection lock in IsUserAuthenticated()");
        #endif
        return false;
    }

    return (m_connection.state == ConnectionState::AUTHENTICATED &&
        m_currentUser.userID != 0);
}

// Get result of last authentication attempt
AuthResult NetworkManager::GetLastAuthResult() const {
    return m_lastAuthResult;
}

// Get current user information
const UserCredentials& NetworkManager::GetCurrentUser() const {
    return m_currentUser;
}

// Send packet with specified command and data
bool NetworkManager::SendPacket(NetworkCommand command, const std::vector<uint8_t>& data) {
    // Route to appropriate protocol handler
    if (m_connection.protocol == NetworkProtocol::TCP) {
        return SendTCPPacket(command, data);
    }
    else {
        return SendUDPPacket(command, data);
    }
}

// Send TCP packet to server
bool NetworkManager::SendTCPPacket(NetworkCommand command, const std::vector<uint8_t>& data) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Sending TCP packet, command: 0x%X, size: %zu",
            static_cast<uint32_t>(command), data.size());
    #endif

    // Must be connected to send packets
    if (!IsConnected()) {
        SetLastError("Not connected to server");
        return false;
    }

    // Create packet with header and data
    NetworkPacket packet;
    packet.header = CreatePacketHeader(command, static_cast<uint32_t>(data.size()));
    packet.data = data;

    // Calculate total packet size
    size_t totalSize = sizeof(NetworkPacketHeader) + data.size();
    std::vector<uint8_t> packetBuffer(totalSize);

    // Copy header to buffer
    memcpy(packetBuffer.data(), &packet.header, sizeof(NetworkPacketHeader));

    // Copy data to buffer if present
    if (!data.empty()) {
        memcpy(packetBuffer.data() + sizeof(NetworkPacketHeader), data.data(), data.size());
    }

    // Send packet over TCP connection
    if (!SendRawData(packetBuffer.data(), totalSize)) {
        SetLastError("Failed to send TCP packet");
        return false;
    }

    // Update statistics
    UpdateNetworkStatistics(true, totalSize);
    m_connection.packetsSent++;

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        LogPacketInfo(packet, true);
    #endif

    return true;
}

// Send UDP packet to server
bool NetworkManager::SendUDPPacket(NetworkCommand command, const std::vector<uint8_t>& data) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Sending UDP packet, command: 0x%X, size: %zu",
            static_cast<uint32_t>(command), data.size());
    #endif

    // Must be connected to send packets
    if (!IsConnected()) {
        SetLastError("Not connected to server");
        return false;
    }

    // Create packet with header and data
    NetworkPacket packet;
    packet.header = CreatePacketHeader(command, static_cast<uint32_t>(data.size()));
    packet.data = data;

    // Calculate total packet size
    size_t totalSize = sizeof(NetworkPacketHeader) + data.size();
    std::vector<uint8_t> packetBuffer(totalSize);

    // Copy header to buffer
    memcpy(packetBuffer.data(), &packet.header, sizeof(NetworkPacketHeader));

    // Copy data to buffer if present
    if (!data.empty()) {
        memcpy(packetBuffer.data() + sizeof(NetworkPacketHeader), data.data(), data.size());
    }

    // Send packet over UDP connection
    if (!SendRawData(packetBuffer.data(), totalSize)) {
        SetLastError("Failed to send UDP packet");
        return false;
    }

    // Update statistics
    UpdateNetworkStatistics(true, totalSize);
    m_connection.packetsSent++;

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        LogPacketInfo(packet, true);
    #endif

    return true;
}

// Process all available incoming packets
bool NetworkManager::ReceivePackets() {
    // Must be connected to receive packets
    if (!IsConnected()) {
        return false;
    }

    bool packetsProcessed = false;
    uint8_t buffer[8192];  // 8KB receive buffer for incoming data

    // Continue receiving while data is available
    while (true) {
        // Attempt to receive data from socket
        int bytesReceived = ReceiveRawData(buffer, sizeof(buffer));

        // No more data available
        if (bytesReceived <= 0) {
            break;
        }

        // Must have at least a complete header
        if (bytesReceived < static_cast<int>(sizeof(NetworkPacketHeader))) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Received incomplete packet header");
            #endif
            m_statistics.packetsDropped++;
            continue;
        }

        // Extract packet header
        NetworkPacketHeader header;
        memcpy(&header, buffer, sizeof(NetworkPacketHeader));

        // Validate packet size
        if (header.packetSize > sizeof(buffer) ||
            header.packetSize < sizeof(NetworkPacketHeader)) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Invalid packet size: %u", header.packetSize);
            #endif
            m_statistics.packetsDropped++;
            continue;
        }

        // Create packet object
        NetworkPacket packet;
        packet.header = header;

        // Extract data portion if present
        uint32_t dataSize = header.packetSize - sizeof(NetworkPacketHeader);
        if (dataSize > 0) {
            packet.data.resize(dataSize);
            memcpy(packet.data.data(), buffer + sizeof(NetworkPacketHeader), dataSize);
        }

        // Validate packet integrity
        if (!ValidatePacket(packet)) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Packet failed validation");
            #endif
            m_statistics.packetsDropped++;
            continue;
        }

        // Add packet to incoming queue
        {
            // Add packet to incoming queue
            {
                ThreadLockHelper packetLock(threadManager, LOCK_PACKET_QUEUE, 1000);
                if (!packetLock.IsLocked()) {
                    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire packet lock for incoming queue");
                    #endif
                    m_statistics.packetsDropped++;
                    continue;
                }
                m_incomingPackets.push(packet);
            }
        }

        // Update statistics
        UpdateNetworkStatistics(false, bytesReceived);
        m_connection.packetsReceived++;
        packetsProcessed = true;

        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            LogPacketInfo(packet, false);
        #endif
    }

    return packetsProcessed;
}

// Check if packets are waiting to be processed
bool NetworkManager::HasPendingPackets() const {
    ThreadLockHelper packetLock(threadManager, LOCK_PACKET_QUEUE, 1000);
    if (!packetLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire packet lock in HasPendingPackets()");
        #endif
        return false;
    }

    return !m_incomingPackets.empty();
}

// Get next packet from receive queue
NetworkPacket NetworkManager::GetNextPacket() {
    ThreadLockHelper packetLock(threadManager, LOCK_PACKET_QUEUE, 1000);
    if (!packetLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire packet lock in GetNextPacket()");
        #endif
        return NetworkPacket(); // Return empty packet if lock fails
    }

    // Return empty packet if queue is empty
    if (m_incomingPackets.empty()) {
        return NetworkPacket();
    }

    // Get packet from front of queue
    NetworkPacket packet = m_incomingPackets.front();
    m_incomingPackets.pop();

    return packet;
}

// Process received command packet
void NetworkManager::ProcessCommand(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing command: 0x%X",
            static_cast<uint32_t>(packet.header.command));
    #endif

    // Look up command handler
    auto handlerIt = m_commandHandlers.find(packet.header.command);
    if (handlerIt != m_commandHandlers.end()) {
        // Execute registered handler
        try {
            handlerIt->second(packet);
        }
        catch (const std::exception& e) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in command handler: %S", e.what());
            #endif
            SetLastError("Exception in command handler: " + std::string(e.what()));
        }
    }
    else {
        // No handler registered for this command
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"No handler for command: 0x%X",
                static_cast<uint32_t>(packet.header.command));
        #endif
    }
}

// Register handler function for specific command
void NetworkManager::RegisterCommandHandler(NetworkCommand command,
    std::function<void(const NetworkPacket&)> handler) {

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Registering handler for command: 0x%X",
            static_cast<uint32_t>(command));
    #endif

    m_commandHandlers[command] = handler;
}

// Send keep-alive ping to server
void NetworkManager::SendPing() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Sending ping to server");
    #endif

    // Send ping command with current timestamp as data
    auto currentTime = std::chrono::steady_clock::now();
    auto timestamp = currentTime.time_since_epoch().count();

    std::vector<uint8_t> pingData(sizeof(timestamp));
    memcpy(pingData.data(), &timestamp, sizeof(timestamp));

    SendPacket(NetworkCommand::CMD_PING, pingData);

    // Update last ping time
    m_connection.lastPingTime = currentTime;
}

// Process received pong response
void NetworkManager::HandlePong(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Received pong from server");
    #endif

    // Calculate round-trip time if timestamp data is present
    if (packet.data.size() >= sizeof(int64_t)) {
        int64_t sentTimestamp;
        memcpy(&sentTimestamp, packet.data.data(), sizeof(sentTimestamp));

        auto currentTime = std::chrono::steady_clock::now();
        auto currentTimestamp = currentTime.time_since_epoch().count();

        // Calculate latency in milliseconds
        float latency = static_cast<float>(currentTimestamp - sentTimestamp) / 1000000.0f;

        // Update average latency (simple moving average)
        if (m_statistics.averageLatency == 0.0f) {
            m_statistics.averageLatency = latency;
        }
        else {
            m_statistics.averageLatency = (m_statistics.averageLatency * 0.8f) + (latency * 0.2f);
        }

#if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Round-trip latency: %.2f ms", latency);
#endif
    }
}

// Calculate simple checksum for packet validation
uint32_t NetworkManager::CalculateChecksum(const uint8_t* data, size_t size) {
    uint32_t checksum = 0;

    // Simple additive checksum
    for (size_t i = 0; i < size; ++i) {
        checksum += data[i];
        checksum = (checksum << 1) | (checksum >> 31);  // Rotate left
    }

    return checksum;
}

// Validate received packet integrity
bool NetworkManager::ValidatePacket(const NetworkPacket& packet) {
    // Check packet ID range (basic validation)
    if (packet.header.packetID == 0 || packet.header.packetID > 0xFFFFFF) {
        return false;
    }

    // Validate packet size consistency
    uint32_t expectedSize = sizeof(NetworkPacketHeader) + static_cast<uint32_t>(packet.data.size());
    if (packet.header.packetSize != expectedSize) {
        return false;
    }

    // Validate checksum if data is present
    if (!packet.data.empty()) {
        uint32_t calculatedChecksum = CalculateChecksum(packet.data.data(), packet.data.size());
        if (packet.header.checksum != calculatedChecksum) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Checksum mismatch: expected 0x%X, got 0x%X",
                    calculatedChecksum, packet.header.checksum);
            #endif
            return false;
        }
    }

    return true;
}

// Start dedicated network processing thread
void NetworkManager::StartNetworkThread() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting network thread");
    #endif

    // Don't start if already running
    if (m_networkThreadRunning.load()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Network thread already running");
        #endif
        return;
    }

    // Set thread running flag
    m_networkThreadRunning.store(true);

    // Create and start network thread using ThreadManager
    if (!threadManager.DoesThreadExist(THREAD_NETWORK)) {
        threadManager.SetThread(THREAD_NETWORK, [this]() { NetworkThreadFunction(); });
    }

    threadManager.StartThread(THREAD_NETWORK);

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread started successfully");
    #endif
}

// Stop network thread gracefully
void NetworkManager::StopNetworkThread() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Stopping network thread");
    #endif

    // Signal thread to stop
    m_networkThreadRunning.store(false);

    // Stop thread through ThreadManager
    if (threadManager.DoesThreadExist(THREAD_NETWORK)) {
        threadManager.StopThread(THREAD_NETWORK);
    }

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread stopped");
    #endif
}

// Main network thread processing loop
void NetworkManager::NetworkThreadFunction() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread function started");
    #endif

    auto lastPingTime = std::chrono::steady_clock::now();

    // Main network processing loop
    while (m_networkThreadRunning.load() &&
        threadManager.GetThreadStatus(THREAD_NETWORK) == ThreadStatus::Running) {

        try {
            // Process incoming packets if connected
            if (IsConnected()) {
                // Receive and queue incoming packets
                ReceivePackets();

                // Process queued packets
                while (HasPendingPackets()) {
                    NetworkPacket packet = GetNextPacket();
                    ProcessCommand(packet);
                }

                // Send periodic ping if interval has elapsed
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastPing = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastPingTime).count();

                if (timeSinceLastPing >= static_cast<long long>(m_pingIntervalMs)) {
                    SendPing();
                    lastPingTime = currentTime;
                }
            }

            // Small delay to prevent excessive CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        }
        catch (const std::exception& e) {
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in network thread: %S", e.what());
            #endif
            SetLastError("Network thread exception: " + std::string(e.what()));

            // Brief pause before continuing
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread function ended");
    #endif
}

// Get current network statistics
const NetworkStatistics& NetworkManager::GetNetworkStatistics() const {
    return m_statistics;
}

// Reset all network statistics counters
void NetworkManager::ResetStatistics() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Resetting network statistics");
    #endif

    m_statistics.bytesReceived = 0;
    m_statistics.bytesSent = 0;
    m_statistics.packetsDropped = 0;
    m_statistics.reconnectAttempts = 0;
    m_statistics.averageLatency = 0.0f;
    m_statistics.sessionStartTime = std::chrono::steady_clock::now();
}

// Get current average network latency
float NetworkManager::GetAverageLatency() const {
    return m_statistics.averageLatency;
}

// Set connection timeout duration
void NetworkManager::SetConnectionTimeout(uint32_t timeoutMs) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Setting connection timeout to %u ms", timeoutMs);
    #endif

    m_connectionTimeoutMs = timeoutMs;
}

// Set ping message interval
void NetworkManager::SetPingInterval(uint32_t intervalMs) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Setting ping interval to %u ms", intervalMs);
    #endif

    m_pingIntervalMs = intervalMs;
}

// Set maximum packet retry attempts
void NetworkManager::SetMaxRetryAttempts(int maxRetries) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Setting max retry attempts to %d", maxRetries);
    #endif

    m_maxRetryAttempts = maxRetries;
}

// Get description of last error
std::string NetworkManager::GetLastErrorMessage() const {
    return m_lastErrorMessage;
}

// Clear stored error message
void NetworkManager::ClearLastError() {
    m_lastErrorMessage.clear();
}

// Initialize Windows Sockets subsystem
bool NetworkManager::InitializeWinsock() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing Winsock");
    #endif

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (result != 0) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"WSAStartup failed with error: %d", result);
        #endif
        return false;
    }

    // Verify Winsock version
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Winsock version 2.2 not available");
        #endif
        WSACleanup();
        return false;
    }

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Winsock initialized successfully");
    #endif

    return true;
}

// Cleanup Windows Sockets
void NetworkManager::CleanupWinsock() {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Cleaning up Winsock");
    #endif

    WSACleanup();
}

// Create socket for specified protocol
SOCKET NetworkManager::CreateSocket(NetworkProtocol protocol) {
    SOCKET sock;

    if (protocol == NetworkProtocol::TCP) {
        // Create TCP socket
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Created TCP socket");
        #endif
    }
    else {
        // Create UDP socket
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Created UDP socket");
        #endif
    }

    if (sock == INVALID_SOCKET) {
        int error = WSAGetLastError();
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to create socket, error: %d", error);
        #endif
    }

    return sock;
}

// Connect socket to server address and port
bool NetworkManager::ConnectSocket(SOCKET sock, const std::string& address, uint16_t port) {
    // Prepare server address structure
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Convert address string to binary format
    if (inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr) <= 0) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid address format: %S", address.c_str());
        #endif
        return false;
    }

    // Attempt connection
    int result = connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Connect failed with error: %d", error);
        #endif
        return false;
    }

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully connected to %S:%d", address.c_str(), port);
    #endif

    return true;
}

// Create packet header with specified command and data size
NetworkPacketHeader NetworkManager::CreatePacketHeader(NetworkCommand command, uint32_t dataSize) {
    NetworkPacketHeader header;

    // Set header fields
    header.packetID = m_expectedPacketID++;                             // Increment packet ID
    header.packetSize = sizeof(NetworkPacketHeader) + dataSize;         // Total packet size
    header.command = command;                                           // Command type
    header.sequenceNumber = m_nextSequenceNumber++;                     // Increment sequence number
    header.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

    // Calculate checksum (will be updated if data is added)
    header.checksum = 0;

    return header;
}

// Send raw data over current connection
bool NetworkManager::SendRawData(const uint8_t* data, size_t size) {
    if (m_connection.socket == INVALID_SOCKET) {
        SetLastError("Invalid socket");
        return false;
    }

    size_t totalSent = 0;

    // Send all data
    while (totalSent < size) {
        int sent = send(m_connection.socket,
            reinterpret_cast<const char*>(data + totalSent),
            static_cast<int>(size - totalSent), 0);

        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Send failed with error: %d", error);
            #endif
            SetLastError("Send failed with error: " + std::to_string(error));
            return false;
        }

        totalSent += sent;
    }

    return true;
}

// Receive raw data from current connection
int NetworkManager::ReceiveRawData(uint8_t* buffer, size_t bufferSize) {
    if (m_connection.socket == INVALID_SOCKET) {
        return -1;
    }

    // Set socket to non-blocking mode for this operation
    u_long nonBlocking = 1;
    ioctlsocket(m_connection.socket, FIONBIO, &nonBlocking);

    int received = recv(m_connection.socket, reinterpret_cast<char*>(buffer),
        static_cast<int>(bufferSize), 0);

    // Restore blocking mode
    nonBlocking = 0;
    ioctlsocket(m_connection.socket, FIONBIO, &nonBlocking);

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {  // WSAEWOULDBLOCK is expected for non-blocking sockets
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Receive failed with error: %d", error);
            #endif
        }
        return 0;  // No data available
    }

    return received;
}

// Handle login response from server
void NetworkManager::HandleLoginResponse(const NetworkPacket& packet) {
#if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing login response");
#endif

    // Parse response data
    if (packet.data.size() < sizeof(uint32_t)) {
        m_lastAuthResult = AuthResult::SERVER_ERROR;
        UpdateConnectionState(ConnectionState::ERROR_STATE);
        return;
    }

    // Extract result code
    uint32_t resultCode;
    memcpy(&resultCode, packet.data.data(), sizeof(resultCode));

    switch (resultCode) {
        case 0: // Success
            m_lastAuthResult = AuthResult::AUTH_SUCCESS;
            UpdateConnectionState(ConnectionState::AUTHENTICATED);

            // Extract user ID if present
            if (packet.data.size() >= sizeof(uint32_t) * 2) {
                memcpy(&m_currentUser.userID, packet.data.data() + sizeof(uint32_t), sizeof(uint32_t));
            }

            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"User authenticated successfully, ID: %u",
                    m_currentUser.userID);
            #endif
            break;

        case 1: // Invalid credentials
            m_lastAuthResult = AuthResult::INVALID_CREDENTIALS;
            UpdateConnectionState(ConnectionState::CONNECTED);
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Authentication failed: Invalid credentials");
            #endif
            break;

        case 2: // User already logged in
            m_lastAuthResult = AuthResult::USER_ALREADY_LOGGED_IN;
            UpdateConnectionState(ConnectionState::CONNECTED);
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Authentication failed: User already logged in");
            #endif
            break;

        default: // Server error
            m_lastAuthResult = AuthResult::SERVER_ERROR;
            UpdateConnectionState(ConnectionState::ERROR_STATE);
            #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Authentication failed: Server error %u", resultCode);
            #endif
            break;
    }
}

// Handle logout response from server
void NetworkManager::HandleLogoutResponse(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing logout response");
    #endif

    // Clear user credentials regardless of server response
    m_currentUser = UserCredentials();
    UpdateConnectionState(ConnectionState::CONNECTED);

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"User logged out");
    #endif
}

// Handle ping command from server
void NetworkManager::HandlePingCommand(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Received ping from server, sending pong");
    #endif

    // Send pong response with same data
    SendPacket(NetworkCommand::CMD_PONG, packet.data);
}

// Handle error notification from server
void NetworkManager::HandleError(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Received error notification from server");
    #endif

    // Extract error message if present
    if (!packet.data.empty()) {
        std::string errorMessage(packet.data.begin(), packet.data.end());
        SetLastError("Server error: " + errorMessage);

        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Server error message: %S", errorMessage.c_str());
        #endif
    }

    UpdateConnectionState(ConnectionState::ERROR_STATE);
}

// Set last error message with timestamp
void NetworkManager::SetLastError(const std::string& errorMessage) {
    m_lastErrorMessage = errorMessage;

    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Network error: %S", errorMessage.c_str());
    #endif
}

// Update connection state with logging
void NetworkManager::UpdateConnectionState(ConnectionState newState) {
    ThreadLockHelper connectionLock(threadManager, LOCK_CONNECTION_STATE, 1000);
    if (!connectionLock.IsLocked()) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire connection lock in UpdateConnectionState()");
        #endif
        return; // Exit function if lock fails
    }

    if (m_connection.state != newState) {
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Connection state changed from %d to %d",
            static_cast<int>(m_connection.state), static_cast<int>(newState));
        #endif

        m_connection.state = newState;
    }
}

// Update network statistics counters
void NetworkManager::UpdateNetworkStatistics(bool sending, size_t bytes) {
    if (sending) {
        m_statistics.bytesSent += bytes;
    }
    else {
        m_statistics.bytesReceived += bytes;
    }
}

// Log network activity for debugging
void NetworkManager::LogNetworkActivity(const std::string& activity) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Network activity: %S", activity.c_str());
    #endif
}

// Log detailed packet information
void NetworkManager::LogPacketInfo(const NetworkPacket& packet, bool sending) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        const wchar_t* direction = sending ? L"SENT" : L"RECEIVED";
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"%s packet - ID: %u, Command: 0x%X, Size: %u, Sequence: %u",
            direction, packet.header.packetID, static_cast<uint32_t>(packet.header.command),
            packet.header.packetSize, packet.header.sequenceNumber);
    #endif
}

#endif // NETWORKMANAGER_CPP