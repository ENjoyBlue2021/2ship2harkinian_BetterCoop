#ifndef NETWORK_ANCHOR_LAN_HOST_SERVER_H
#define NETWORK_ANCHOR_LAN_HOST_SERVER_H
#ifdef __cplusplus

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <SDL2/SDL_net.h>
#include <nlohmann/json.hpp>

/**
 * Lightweight embedded TCP relay/host server for direct LAN co-op.
 * Runs in a background thread inside the Host's 2Ship process using SDL2_net.
 * Client ID 1 is always reserved for the local Host (127.0.0.1), making Host authoritative.
 */
class LanHostServer {
  public:
    LanHostServer() = default;
    ~LanHostServer();

    bool Start(uint16_t port);
    void Stop();

    bool IsRunning() const {
        return isRunning.load();
    }
    uint16_t GetPort() const {
        return listenPort;
    }
    std::string GetLastError() const;
    size_t GetConnectedClientCount() const;

    // Best-effort helper to display this machine's LAN IP in the UI
    static std::string GetLocalLanIp();

  private:
    struct ClientConnection {
        uint32_t clientId = 0;
        TCPsocket socket = nullptr;
        std::string recvBuffer;
        nlohmann::json clientState;
        bool handshakeComplete = false;
    };

    void ServerLoop();
    void AcceptNewClients();
    void ProcessClientData(uint32_t clientId);
    void HandlePacket(uint32_t senderId, nlohmann::json& payload);
    void DisconnectClient(uint32_t clientId);
    void BroadcastAllClientState();
    void SendJsonToClient(TCPsocket sock, const nlohmann::json& payload);

    std::atomic<bool> isRunning{ false };
    uint16_t listenPort = 43383;
    TCPsocket listenSocket = nullptr;
    std::thread serverThread;

    mutable std::mutex stateMutex;
    std::string lastError;
    uint32_t nextClientId = 2;
    std::map<uint32_t, ClientConnection> clients;
    nlohmann::json roomState;
};

#endif // __cplusplus
#endif // NETWORK_ANCHOR_LAN_HOST_SERVER_H
