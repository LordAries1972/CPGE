// -------------------------------------------------------------------------------------------------------------
// NetworkManager.h - Network communication interface for TCP/UDP packet handling
// This class provides comprehensive networking capabilities including authentication,
// command processing, and threaded network operations for optimal performance.
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "ThreadManager.h"
#include "Debug.h"
#include "Vectors.h"

// CRITICAL: Windows networking headers must be included in correct order
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN                                             // Exclude rarely-used Windows APIs
#endif

#include <windows.h>                                                    // Must be included before winsock2.h
#include <winsock2.h>                                                   // Main Winsock API
#include <ws2tcpip.h>                                                   // Additional TCP/IP functions
#include <iphlpapi.h>                                                   // IP Helper API
#include <vector>                                                       // STL vector container
#include <queue>                                                        // STL queue container
#include <unordered_map>                                                // STL hash map container
#include <chrono>                                                       // Time utilities
#include <atomic>                                                       // Atomic operations
#include <mutex>                                                        // Mutex synchronization
#include <thread>                                                       // Thread support
#include <functional>                                                   // Function objects

// Link with required Windows libraries
#pragma comment(lib, "ws2_32.lib")                                      // Winsock 2 library
#pragma comment(lib, "iphlpapi.lib")                                    // IP Helper API library

// Forward declarations
extern Debug debug;
extern ThreadManager threadManager;

// Network protocol types
enum class NetworkProtocol {
    TCP,                                                                // Transmission Control Protocol - reliable, ordered delivery
    UDP                                                                 // User Datagram Protocol - fast, unreliable delivery
};

// Network connection states
enum class ConnectionState {
    DISCONNECTED,                                                       // No active connection
    CONNECTING,                                                         // Attempting to establish connection
    CONNECTED,                                                          // Successfully connected but not authenticated
    AUTHENTICATING,                                                     // In process of user authentication
    AUTHENTICATED,                                                      // User successfully authenticated
    ERROR_STATE,                                                        // Connection error occurred
    RECONNECTING                                                        // Attempting to reconnect after disconnection
};

// Network command types for server communication
enum class NetworkCommand : uint32_t {
    // Authentication commands
    CMD_LOGIN_REQUEST = 0x1001,                                         // Request user login authentication
    CMD_LOGIN_RESPONSE = 0x1002,                                        // Server response to login request
    CMD_LOGOUT_REQUEST = 0x1003,                                        // Request user logout
    CMD_LOGOUT_RESPONSE = 0x1004,                                       // Server response to logout request

    // Connection management commands
    CMD_PING = 0x2001,                                                  // Keep-alive ping message
    CMD_PONG = 0x2002,                                                  // Response to ping message
    CMD_DISCONNECT = 0x2003,                                            // Graceful disconnection request

    // Game-specific commands
    CMD_GAME_UPDATE = 0x3001,                                           // Game state update information
    CMD_PLAYER_ACTION = 0x3002,                                         // Player input/action data
    CMD_CHAT_MESSAGE = 0x3003,                                          // Chat message transmission

    // System commands
    CMD_ERROR = 0x9001,                                                 // Error notification
    CMD_UNKNOWN = 0x9999                                                // Unknown/invalid command type
};

// Authentication result codes
enum class AuthResult {
    AUTH_SUCCESS,                                                       // Authentication successful
    INVALID_CREDENTIALS,                                                // Username/password incorrect
    USER_ALREADY_LOGGED_IN,                                             // User already has active session
    SERVER_ERROR,                                                       // Server-side authentication error
    TIMEOUT,                                                            // Authentication request timed out
    NETWORK_ERROR                                                       // Network communication failed
};

// Network packet header structure - all packets must begin with this header
#pragma pack(push, 1)                                                  // Ensure exact byte alignment for network transmission
struct NetworkPacketHeader {
    uint32_t packetID;                                                  // Unique packet identifier for validation
    uint32_t packetSize;                                                // Total size of packet including header
    NetworkCommand command;                                             // Command type for packet processing
    uint32_t sequenceNumber;                                            // Packet sequence for ordering/duplicate detection
    uint32_t checksum;                                                  // Simple checksum for basic packet validation
    uint64_t timestamp;                                                 // Packet creation timestamp
};
#pragma pack(pop)                                                       // Restore default alignment

// User authentication data structure
struct UserCredentials {
    std::string username;                                               // User account name
    std::string password;                                               // User account password (should be hashed)
    std::string sessionToken;                                          // Session authentication token
    uint32_t userID;                                                    // Unique user identifier from server
    std::chrono::steady_clock::time_point lastActivity;                // Last activity timestamp for timeout detection

    // Constructor with default initialization
    UserCredentials() : userID(0), lastActivity(std::chrono::steady_clock::now()) {}
};

// Network packet data container
struct NetworkPacket {
    NetworkPacketHeader header;                                         // Standard packet header
    std::vector<uint8_t> data;                                          // Variable-length packet payload data
    std::chrono::steady_clock::time_point sendTime;                     // Time packet was sent for timeout detection
    int retryCount;                                                     // Number of transmission attempts

    // Constructor with default initialization
    NetworkPacket() : sendTime(std::chrono::steady_clock::now()), retryCount(0) {}
};

// Network connection information
struct NetworkConnection {
    SOCKET socket;                                                      // Winsock socket handle
    NetworkProtocol protocol;                                           // Connection protocol type (TCP/UDP)
    std::string serverAddress;                                          // Server IP address or hostname
    uint16_t serverPort;                                                // Server port number
    ConnectionState state;                                              // Current connection state
    std::chrono::steady_clock::time_point lastPingTime;                 // Last ping message timestamp
    std::chrono::steady_clock::time_point connectTime;                  // Connection establishment time
    uint32_t packetsReceived;                                           // Total packets received counter
    uint32_t packetsSent;                                               // Total packets sent counter

    // Constructor with default initialization
    NetworkConnection() : socket(INVALID_SOCKET), protocol(NetworkProtocol::TCP),
        serverPort(0), state(ConnectionState::DISCONNECTED),
        packetsReceived(0), packetsSent(0) {
    }
};

// Network statistics for monitoring and debugging
struct NetworkStatistics {
    uint64_t bytesReceived;                                             // Total bytes received
    uint64_t bytesSent;                                                 // Total bytes sent
    uint32_t packetsDropped;                                            // Packets lost or corrupted
    uint32_t reconnectAttempts;                                         // Number of reconnection attempts
    float averageLatency;                                               // Average round-trip time in milliseconds
    std::chrono::steady_clock::time_point sessionStartTime;             // Session start timestamp

    // Constructor with default initialization
    NetworkStatistics() : bytesReceived(0), bytesSent(0), packetsDropped(0),
        reconnectAttempts(0), averageLatency(0.0f),
        sessionStartTime(std::chrono::steady_clock::now()) {
    }
};

// Main NetworkManager class for all network operations
class NetworkManager {
public:
    // Constructor and destructor
    NetworkManager();                                                   // Initialize networking subsystem
    ~NetworkManager();                                                  // Clean up network resources

    // Core networking functions
    bool Initialize();                                                  // Initialize Winsock and network subsystem
    void Cleanup();                                                     // Clean up all network resources and connections

    // Connection management
    bool ConnectToServer(const std::string& serverAddress, uint16_t port, NetworkProtocol protocol = NetworkProtocol::TCP);
    void DisconnectFromServer();                                        // Gracefully disconnect from server
    bool IsConnected() const;                                           // Check if currently connected to server
    ConnectionState GetConnectionState() const;                         // Get current connection state

    // Authentication functions
    bool AuthenticateUser(const std::string& username, const std::string& password);
    bool LogoutUser();                                                  // Logout current authenticated user
    bool IsUserAuthenticated() const;                                   // Check if user is currently authenticated
    AuthResult GetLastAuthResult() const;                               // Get result of last authentication attempt
    const UserCredentials& GetCurrentUser() const;                     // Get current user information

    // Packet transmission functions
    bool SendPacket(NetworkCommand command, const std::vector<uint8_t>& data = {});
    bool SendTCPPacket(NetworkCommand command, const std::vector<uint8_t>& data);
    bool SendUDPPacket(NetworkCommand command, const std::vector<uint8_t>& data);

    // Packet reception functions
    bool ReceivePackets();                                              // Process all available incoming packets
    bool HasPendingPackets() const;                                     // Check if packets are waiting to be processed
    NetworkPacket GetNextPacket();                                      // Get next packet from receive queue

    // Command processing
    void ProcessCommand(const NetworkPacket& packet);                   // Process received command packet
    void RegisterCommandHandler(NetworkCommand command, std::function<void(const NetworkPacket&)> handler);

    // Network utilities
    void SendPing();                                                    // Send keep-alive ping to server
    void HandlePong(const NetworkPacket& packet);                       // Process received pong response
    uint32_t CalculateChecksum(const uint8_t* data, size_t size);       // Calculate simple packet checksum
    bool ValidatePacket(const NetworkPacket& packet);                   // Validate received packet integrity

    // Thread management
    void StartNetworkThread();                                          // Start dedicated network processing thread
    void StopNetworkThread();                                           // Stop network thread gracefully
    void NetworkThreadFunction();                                       // Main network thread processing loop

    // Statistics and monitoring
    const NetworkStatistics& GetNetworkStatistics() const;             // Get current network statistics
    void ResetStatistics();                                             // Reset all network statistics counters
    float GetAverageLatency() const;                                    // Get current average network latency

    // Configuration
    void SetConnectionTimeout(uint32_t timeoutMs);                      // Set connection timeout duration
    void SetPingInterval(uint32_t intervalMs);                          // Set ping message interval
    void SetMaxRetryAttempts(int maxRetries);                           // Set maximum packet retry attempts

    // Error handling
    std::string GetLastErrorMessage() const;                            // Get description of last error
    void ClearLastError();                                              // Clear stored error message

private:
    // Private member variables
    bool m_isInitialized;                                               // Network subsystem initialization state
    bool m_isCleanedUp;                                                 // Cleanup completion flag

    // Network connection data
    NetworkConnection m_connection;                                     // Primary server connection
    UserCredentials m_currentUser;                                      // Current authenticated user data
    AuthResult m_lastAuthResult;                                        // Result of last authentication attempt

    // Packet management
    std::queue<NetworkPacket> m_incomingPackets;                        // Queue of received packets awaiting processing
    std::queue<NetworkPacket> m_outgoingPackets;                        // Queue of packets waiting to be sent
    std::unordered_map<NetworkCommand, std::function<void(const NetworkPacket&)>> m_commandHandlers;

    // Threading and synchronization
    std::atomic<bool> m_networkThreadRunning;                           // Network thread execution flag

    // Thread lock names for ThreadManager integration
    const std::string LOCK_PACKET_QUEUE = "network_packet_queue";       // Lock name for packet queue operations
    const std::string LOCK_CONNECTION_STATE = "network_connection_state"; // Lock name for connection state operations

    // Network statistics and monitoring
    NetworkStatistics m_statistics;                                     // Current session statistics
    std::string m_lastErrorMessage;                                     // Last error message for debugging

    // Configuration parameters
    uint32_t m_connectionTimeoutMs;                                     // Connection timeout in milliseconds
    uint32_t m_pingIntervalMs;                                          // Ping interval in milliseconds
    int m_maxRetryAttempts;                                             // Maximum packet transmission retries
    uint32_t m_nextSequenceNumber;                                      // Next packet sequence number
    uint32_t m_expectedPacketID;                                        // Expected next packet ID for validation

    // Private helper functions
    bool InitializeWinsock();                                           // Initialize Windows Sockets
    void CleanupWinsock();                                              // Cleanup Windows Sockets
    SOCKET CreateSocket(NetworkProtocol protocol);                      // Create socket for specified protocol
    bool ConnectSocket(SOCKET sock, const std::string& address, uint16_t port);

    // Packet processing helpers
    NetworkPacketHeader CreatePacketHeader(NetworkCommand command, uint32_t dataSize);
    bool SendRawData(const uint8_t* data, size_t size);                 // Send raw data over current connection
    int ReceiveRawData(uint8_t* buffer, size_t bufferSize);             // Receive raw data from current connection

    // Command handlers (default implementations)
    void HandleLoginResponse(const NetworkPacket& packet);              // Process login response from server
    void HandleLogoutResponse(const NetworkPacket& packet);             // Process logout response from server
    void HandlePingCommand(const NetworkPacket& packet);                // Process ping command from server
    void HandleError(const NetworkPacket& packet);                      // Process error notification from server

    // Utility functions
    void SetLastError(const std::string& errorMessage);                 // Set last error message with timestamp
    void UpdateConnectionState(ConnectionState newState);               // Update connection state with logging
    void UpdateNetworkStatistics(bool sending, size_t bytes);           // Update network statistics counters

    // Debug and logging helpers
    void LogNetworkActivity(const std::string& activity);               // Log network activity for debugging
    void LogPacketInfo(const NetworkPacket& packet, bool sending);      // Log detailed packet information
};

// Global NetworkManager instance declaration
extern NetworkManager networkManager;
