# NetworkManager Class - Comprehensive Usage Guide

## Table of Contents
1. [Overview](#overview)
2. [Basic Setup and Initialization](#basic-setup-and-initialization)
3. [Connection Management](#connection-management)
4. [Authentication System](#authentication-system)
5. [Packet Communication](#packet-communication)
6. [Command Handling](#command-handling)
7. [Thread Management](#thread-management)
8. [Error Handling](#error-handling)
9. [Network Statistics](#network-statistics)
10. [Advanced Features](#advanced-features)
11. [Complete Usage Examples](#complete-usage-examples)
12. [Troubleshooting](#troubleshooting)

---

## Overview

The NetworkManager class provides comprehensive TCP/UDP networking capabilities for game engines, featuring:
- **Dual Protocol Support**: Both TCP (reliable) and UDP (fast) communications
- **Authentication System**: Secure user login/logout with session management
- **Command Processing**: Extensible command handling system
- **Thread Safety**: Dedicated network thread with ThreadManager integration
- **Packet Validation**: Checksums and sequence numbers for data integrity
- **Statistics Monitoring**: Real-time network performance tracking

---

## Basic Setup and Initialization

### 1. Include Required Files

Add to your source file:
```cpp
#include "NetworkManager.h"
#include "ThreadLockHelper.h"
```

### 2. Global Instance Access

The NetworkManager is available as a global instance:
```cpp
extern NetworkManager networkManager;
```

### 3. Initialize the System

**In your main initialization code (typically in WinMain or similar):**
```cpp
// Initialize Network Manager
if (!networkManager.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Network system initialization failed.");
    return EXIT_FAILURE;
}

#if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"NetworkManager initialized successfully");
#endif
```

### 4. Cleanup on Exit

**In your cleanup section:**
```cpp
// Clean up Network Manager
networkManager.Cleanup();
```

---

## Connection Management

### Connecting to a Server

**TCP Connection (Default - Reliable):**
```cpp
bool success = networkManager.ConnectToServer("192.168.1.100", 8080, NetworkProtocol::TCP);
if (success) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Connected to game server via TCP");
} else {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to connect: " + 
        StringToWString(networkManager.GetLastErrorMessage()));
}
```

**UDP Connection (Fast - Unreliable):**
```cpp
bool success = networkManager.ConnectToServer("game.server.com", 7777, NetworkProtocol::UDP);
if (success) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Connected to game server via UDP");
}
```

### Checking Connection Status

```cpp
// Simple connection check
if (networkManager.IsConnected()) {
    // Perform network operations
}

// Detailed state checking
ConnectionState state = networkManager.GetConnectionState();
switch (state) {
    case ConnectionState::CONNECTED:
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Connected to server");
        break;
    case ConnectionState::AUTHENTICATING:
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Authentication in progress");
        break;
    case ConnectionState::AUTHENTICATED:
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Authenticated and ready");
        break;
    case ConnectionState::ERROR_STATE:
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Connection error occurred");
        break;
}
```

### Disconnecting from Server

```cpp
// Graceful disconnect
networkManager.DisconnectFromServer();
debug.logLevelMessage(LogLevel::LOG_INFO, L"Disconnected from server");
```

---

## Authentication System

### User Login

```cpp
bool authenticateUser(const std::string& username, const std::string& password) {
    // Ensure we're connected first
    if (!networkManager.IsConnected()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Not connected to server");
        return false;
    }
    
    // Send authentication request
    bool authSent = networkManager.AuthenticateUser(username, password);
    if (!authSent) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to send authentication request");
        return false;
    }
    
    // Wait for authentication response (in practice, handle this in your game loop)
    // The response will be processed automatically by the network thread
    return true;
}
```

### Checking Authentication Result

```cpp
void checkAuthenticationStatus() {
    AuthResult result = networkManager.GetLastAuthResult();
    
    switch (result) {
        case AuthResult::SUCCESS:
            debug.logLevelMessage(LogLevel::LOG_INFO, L"User authenticated successfully");
            // Get user information
            const UserCredentials& user = networkManager.GetCurrentUser();
            debug.logDebugMessage(LogLevel::LOG_INFO, L"User ID: %u, Username: %S", 
                user.userID, user.username.c_str());
            break;
            
        case AuthResult::INVALID_CREDENTIALS:
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Invalid username or password");
            break;
            
        case AuthResult::USER_ALREADY_LOGGED_IN:
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"User already logged in elsewhere");
            break;
            
        case AuthResult::SERVER_ERROR:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Server authentication error");
            break;
            
        case AuthResult::NETWORK_ERROR:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Network communication failed");
            break;
    }
}
```

### User Logout

```cpp
void logoutCurrentUser() {
    if (networkManager.IsUserAuthenticated()) {
        bool success = networkManager.LogoutUser();
        if (success) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"User logged out successfully");
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Logout failed: " + 
                StringToWString(networkManager.GetLastErrorMessage()));
        }
    }
}
```

---

## Packet Communication

### Sending Simple Commands

```cpp
// Send ping to server
networkManager.SendPing();

// Send disconnect request
networkManager.SendPacket(NetworkCommand::CMD_DISCONNECT);

// Send chat message
void sendChatMessage(const std::string& message) {
    std::vector<uint8_t> chatData(message.begin(), message.end());
    bool success = networkManager.SendPacket(NetworkCommand::CMD_CHAT_MESSAGE, chatData);
    
    if (success) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Sent chat message: %S", message.c_str());
    } else {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to send chat message");
    }
}
```

### Sending Game Data

```cpp
// Send player position update
void sendPlayerUpdate(float x, float y, float z, float yaw, float pitch) {
    std::vector<uint8_t> updateData;
    
    // Pack position data
    updateData.insert(updateData.end(), reinterpret_cast<uint8_t*>(&x), 
                     reinterpret_cast<uint8_t*>(&x) + sizeof(x));
    updateData.insert(updateData.end(), reinterpret_cast<uint8_t*>(&y), 
                     reinterpret_cast<uint8_t*>(&y) + sizeof(y));
    updateData.insert(updateData.end(), reinterpret_cast<uint8_t*>(&z), 
                     reinterpret_cast<uint8_t*>(&z) + sizeof(z));
    updateData.insert(updateData.end(), reinterpret_cast<uint8_t*>(&yaw), 
                     reinterpret_cast<uint8_t*>(&yaw) + sizeof(yaw));
    updateData.insert(updateData.end(), reinterpret_cast<uint8_t*>(&pitch), 
                     reinterpret_cast<uint8_t*>(&pitch) + sizeof(pitch));
    
    // Send as player action
    bool success = networkManager.SendPacket(NetworkCommand::CMD_PLAYER_ACTION, updateData);
    
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        if (success) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Sent player update: pos(%.2f, %.2f, %.2f) rot(%.2f, %.2f)", 
                x, y, z, yaw, pitch);
        }
    #endif
}
```

### Protocol-Specific Sending

```cpp
// Force TCP for critical data
bool sendCriticalData(const std::vector<uint8_t>& data) {
    return networkManager.SendTCPPacket(NetworkCommand::CMD_GAME_UPDATE, data);
}

// Force UDP for frequent updates
bool sendFrequentUpdate(const std::vector<uint8_t>& data) {
    return networkManager.SendUDPPacket(NetworkCommand::CMD_PLAYER_ACTION, data);
}
```

---

## Command Handling

### Registering Custom Command Handlers

```cpp
void setupNetworkHandlers() {
    // Register handler for game updates
    networkManager.RegisterCommandHandler(NetworkCommand::CMD_GAME_UPDATE, 
        [](const NetworkPacket& packet) {
            handleGameUpdate(packet);
        });
    
    // Register handler for chat messages
    networkManager.RegisterCommandHandler(NetworkCommand::CMD_CHAT_MESSAGE, 
        [](const NetworkPacket& packet) {
            handleChatMessage(packet);
        });
    
    // Register handler for player actions
    networkManager.RegisterCommandHandler(NetworkCommand::CMD_PLAYER_ACTION, 
        [](const NetworkPacket& packet) {
            handlePlayerAction(packet);
        });
}
```

### Implementing Command Handlers

```cpp
void handleGameUpdate(const NetworkPacket& packet) {
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing game update, size: %zu bytes", 
            packet.data.size());
    #endif
    
    // Parse game update data
    if (packet.data.size() >= sizeof(uint32_t)) {
        uint32_t updateType;
        memcpy(&updateType, packet.data.data(), sizeof(updateType));
        
        // Process based on update type
        switch (updateType) {
            case 1: // World state update
                processWorldUpdate(packet.data);
                break;
            case 2: // Player list update
                processPlayerListUpdate(packet.data);
                break;
            default:
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Unknown update type: %u", updateType);
                break;
        }
    }
}

void handleChatMessage(const NetworkPacket& packet) {
    // Extract chat message string
    if (!packet.data.empty()) {
        std::string chatMessage(packet.data.begin(), packet.data.end());
        
        #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Received chat: %S", chatMessage.c_str());
        #endif
        
        // Display in game chat system
        displayChatMessage(chatMessage);
    }
}

void handlePlayerAction(const NetworkPacket& packet) {
    // Parse player position data
    if (packet.data.size() >= sizeof(float) * 5) {
        float x, y, z, yaw, pitch;
        size_t offset = 0;
        
        memcpy(&x, packet.data.data() + offset, sizeof(x)); offset += sizeof(x);
        memcpy(&y, packet.data.data() + offset, sizeof(y)); offset += sizeof(y);
        memcpy(&z, packet.data.data() + offset, sizeof(z)); offset += sizeof(z);
        memcpy(&yaw, packet.data.data() + offset, sizeof(yaw)); offset += sizeof(yaw);
        memcpy(&pitch, packet.data.data() + offset, sizeof(pitch));
        
        // Update player position in game world
        updatePlayerPosition(packet.header.packetID, x, y, z, yaw, pitch);
    }
}
```

---

## Thread Management

### Starting Network Thread

```cpp
void initializeNetworking() {
    // Initialize network system
    if (!networkManager.Initialize()) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize networking");
        return;
    }
    
    // Setup command handlers
    setupNetworkHandlers();
    
    // Start dedicated network thread
    networkManager.StartNetworkThread();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Network system ready");
}
```

### Processing Packets in Game Loop

```cpp
void gameLoop() {
    while (gameRunning) {
        // Process input
        handleInput();
        
        // Process network packets (if not using dedicated thread)
        if (networkManager.IsConnected()) {
            // Receive incoming packets
            networkManager.ReceivePackets();
            
            // Process queued packets
            while (networkManager.HasPendingPackets()) {
                NetworkPacket packet = networkManager.GetNextPacket();
                networkManager.ProcessCommand(packet);
            }
        }
        
        // Update game logic
        updateGame();
        
        // Render frame
        renderFrame();
    }
}
```

### Stopping Network Thread

```cpp
void shutdownNetworking() {
    // Stop network thread
    networkManager.StopNetworkThread();
    
    // Disconnect from server
    if (networkManager.IsConnected()) {
        networkManager.DisconnectFromServer();
    }
    
    // Clean up network resources
    networkManager.Cleanup();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Network system shut down");
}
```

---

## Error Handling

### Checking for Errors

```cpp
void checkNetworkErrors() {
    std::string lastError = networkManager.GetLastErrorMessage();
    if (!lastError.empty()) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Network error: %S", lastError.c_str());
        
        // Clear error after handling
        networkManager.ClearLastError();
        
        // Take appropriate action based on error
        handleNetworkError(lastError);
    }
}

void handleNetworkError(const std::string& error) {
    // Check connection state
    ConnectionState state = networkManager.GetConnectionState();
    
    if (state == ConnectionState::ERROR_STATE) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Connection in error state, attempting reconnect");
        
        // Attempt to reconnect
        attemptReconnection();
    }
}
```

### Automatic Reconnection

```cpp
void attemptReconnection() {
    static auto lastReconnectAttempt = std::chrono::steady_clock::now();
    static int reconnectAttempts = 0;
    const int maxReconnectAttempts = 5;
    const auto reconnectDelay = std::chrono::seconds(5);
    
    auto currentTime = std::chrono::steady_clock::now();
    
    // Check if enough time has passed since last attempt
    if (currentTime - lastReconnectAttempt >= reconnectDelay) {
        if (reconnectAttempts < maxReconnectAttempts) {
            reconnectAttempts++;
            lastReconnectAttempt = currentTime;
            
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Reconnection attempt %d/%d", 
                reconnectAttempts, maxReconnectAttempts);
            
            // Try to reconnect
            bool success = networkManager.ConnectToServer(lastServerAddress, lastServerPort);
            if (success) {
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Reconnection successful");
                reconnectAttempts = 0; // Reset counter
            }
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Maximum reconnection attempts reached");
            // Handle permanent disconnection
        }
    }
}
```

---

## Network Statistics

### Monitoring Performance

```cpp
void displayNetworkStats() {
    const NetworkStatistics& stats = networkManager.GetNetworkStatistics();
    
    // Calculate session duration
    auto sessionDuration = std::chrono::steady_clock::now() - stats.sessionStartTime;
    auto sessionMinutes = std::chrono::duration_cast<std::chrono::minutes>(sessionDuration).count();
    
    #if defined(_DEBUG_NETWORKMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Network Statistics:");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Session Duration: %lld minutes", sessionMinutes);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Bytes Sent: %llu", stats.bytesSent);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Bytes Received: %llu", stats.bytesReceived);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Packets Dropped: %u", stats.packetsDropped);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Average Latency: %.2f ms", stats.averageLatency);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Reconnect Attempts: %u", stats.reconnectAttempts);
    #endif
}

void monitorNetworkPerformance() {
    static auto lastStatsDisplay = std::chrono::steady_clock::now();
    const auto statsInterval = std::chrono::seconds(30);
    
    auto currentTime = std::chrono::steady_clock::now();
    
    // Display stats every 30 seconds
    if (currentTime - lastStatsDisplay >= statsInterval) {
        displayNetworkStats();
        lastStatsDisplay = currentTime;
        
        // Check for performance issues
        float latency = networkManager.GetAverageLatency();
        if (latency > 200.0f) { // High latency threshold
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"High network latency detected");
        }
    }
}
```

---

## Advanced Features

### Custom Packet Validation

```cpp
bool validateCustomPacket(const NetworkPacket& packet) {
    // Check packet sequence for ordering
    static uint32_t expectedSequence = 1;
    
    if (packet.header.sequenceNumber < expectedSequence) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Received out-of-order packet");
        return false; // Duplicate or old packet
    }
    
    expectedSequence = packet.header.sequenceNumber + 1;
    
    // Additional custom validation
    if (packet.header.command == NetworkCommand::CMD_GAME_UPDATE) {
        // Validate game update specific requirements
        if (packet.data.size() < sizeof(uint32_t)) {
            return false;
        }
    }
    
    return true;
}
```

### Configuration Management

```cpp
void configureNetworkSettings() {
    // Set connection timeout (30 seconds)
    networkManager.SetConnectionTimeout(30000);
    
    // Set ping interval (every 15 seconds)
    networkManager.SetPingInterval(15000);
    
    // Set maximum retry attempts
    networkManager.SetMaxRetryAttempts(3);
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Network settings configured");
}
```

---

## Complete Usage Examples

### Basic Client Setup

```cpp
class GameClient {
private:
    std::string serverAddress;
    uint16_t serverPort;
    bool isNetworkReady;
    
public:
    bool initializeNetwork(const std::string& address, uint16_t port) {
        serverAddress = address;
        serverPort = port;
        
        // Initialize network manager
        if (!networkManager.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize network");
            return false;
        }
        
        // Configure settings
        configureNetworkSettings();
        
        // Setup command handlers
        setupGameHandlers();
        
        // Start network thread
        networkManager.StartNetworkThread();
        
        isNetworkReady = true;
        return true;
    }
    
    bool connectToServer() {
        if (!isNetworkReady) return false;
        
        bool connected = networkManager.ConnectToServer(serverAddress, serverPort, NetworkProtocol::TCP);
        if (connected) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Connected to game server");
        }
        return connected;
    }
    
    bool loginUser(const std::string& username, const std::string& password) {
        if (!networkManager.IsConnected()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Not connected to server");
            return false;
        }
        
        return networkManager.AuthenticateUser(username, password);
    }
    
    void update() {
        // Monitor network performance
        monitorNetworkPerformance();
        
        // Check for errors
        checkNetworkErrors();
        
        // Send periodic updates
        sendPlayerUpdates();
    }
    
    void shutdown() {
        if (networkManager.IsUserAuthenticated()) {
            networkManager.LogoutUser();
        }
        
        if (networkManager.IsConnected()) {
            networkManager.DisconnectFromServer();
        }
        
        networkManager.StopNetworkThread();
        networkManager.Cleanup();
        
        isNetworkReady = false;
    }
    
private:
    void setupGameHandlers() {
        networkManager.RegisterCommandHandler(NetworkCommand::CMD_GAME_UPDATE, 
            [this](const NetworkPacket& packet) { handleGameUpdate(packet); });
        networkManager.RegisterCommandHandler(NetworkCommand::CMD_CHAT_MESSAGE, 
            [this](const NetworkPacket& packet) { handleChatMessage(packet); });
    }
    
    void sendPlayerUpdates() {
        static auto lastUpdate = std::chrono::steady_clock::now();
        const auto updateInterval = std::chrono::milliseconds(50); // 20fps
        
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - lastUpdate >= updateInterval) {
            // Get current player state
            Vector3 position = getPlayerPosition();
            float yaw = getPlayerYaw();
            float pitch = getPlayerPitch();
            
            // Send update
            sendPlayerUpdate(position.x, position.y, position.z, yaw, pitch);
            lastUpdate = currentTime;
        }
    }
};
```

### Usage in Main Game Loop

```cpp
int main() {
    GameClient client;
    
    // Initialize network
    if (!client.initializeNetwork("game.server.com", 8080)) {
        return -1;
    }
    
    // Connect to server
    if (!client.connectToServer()) {
        return -1;
    }
    
    // Login user
    if (!client.loginUser("player1", "password123")) {
        return -1;
    }
    
    // Main game loop
    while (gameRunning) {
        client.update();
        
        // Your game logic here
        updateGame();
        renderFrame();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
    }
    
    // Cleanup
    client.shutdown();
    return 0;
}
```

---

## Troubleshooting

### Common Issues and Solutions

#### Connection Failures
```cpp
// Check if Winsock initialization failed
if (!networkManager.Initialize()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Winsock initialization failed - check Windows Sockets");
}

// Check firewall/network connectivity
bool canConnect = networkManager.ConnectToServer("8.8.8.8", 53); // Test with known service
```

#### Authentication Problems
```cpp
// Check authentication result
AuthResult result = networkManager.GetLastAuthResult();
if (result == AuthResult::TIMEOUT) {
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Authentication timeout - server may be overloaded");
} else if (result == AuthResult::NETWORK_ERROR) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Network error during authentication");
}
```

#### High Latency Issues
```cpp
// Monitor and adjust ping interval
float latency = networkManager.GetAverageLatency();
if (latency > 150.0f) {
    // Increase ping interval to reduce network overhead
    networkManager.SetPingInterval(45000); // 45 seconds
}
```

#### Packet Loss
```cpp
const NetworkStatistics& stats = networkManager.GetNetworkStatistics();
float packetLossRate = static_cast<float>(stats.packetsDropped) / 
                      static_cast<float>(stats.packetsDropped + connection.packetsReceived);

if (packetLossRate > 0.05f) { // 5% loss threshold
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"High packet loss detected - consider switching to TCP");
}
```

### Debug Output Configuration

Enable detailed network debugging by defining in Debug.h:
```cpp
#define _DEBUG_NETWORKMANAGER_  // Enable NetworkManager debug output
```

This will provide detailed logging of:
- Connection establishment/termination
- Packet transmission/reception
- Authentication processes
- Error conditions
- Performance statistics

---

## Summary

The NetworkManager class provides a robust, thread-safe networking solution with:

✅ **Easy Integration** - Simple initialization and cleanup  
✅ **Flexible Protocols** - Support for both TCP and UDP  
✅ **Secure Authentication** - Built-in user login/logout system  
✅ **Command System** - Extensible packet command handling  
✅ **Thread Safety** - Uses ThreadManager and ThreadLockHelper  
✅ **Performance Monitoring** - Real-time statistics and latency tracking  
✅ **Error Recovery** - Comprehensive error handling and reconnection  
✅ **Debug Support** - Detailed logging for troubleshooting  

Follow this guide to integrate robust networking into your game engine efficiently and safely.