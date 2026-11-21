#include "LanHostServer.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static constexpr size_t MAX_LAN_CLIENTS = 4;

LanHostServer::~LanHostServer() {
    Stop();
}

bool LanHostServer::Start(uint16_t port) {
    Stop();

    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, nullptr, port) == -1) {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError = std::string("Failed to resolve host port: ") + SDLNet_GetError();
        SPDLOG_ERROR("[LanHostServer] {}", lastError);
        return false;
    }

    listenSocket = SDLNet_TCP_Open(&ip);
    if (!listenSocket) {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError = std::string("Failed to open port ") + std::to_string(port) + ": " + SDLNet_GetError();
        SPDLOG_ERROR("[LanHostServer] {}", lastError);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError.clear();
        listenPort = port;
        nextClientId = 2;
        clients.clear();
        roomState = {
            { "ownerClientId", 1 },
            { "pvpMode", 0 },
            { "teleportMode", 2 },
            { "showLocationsMode", 2 },
            { "syncItemsAndFlags", 1 },
        };
    }

    isRunning = true;
    serverThread = std::thread(&LanHostServer::ServerLoop, this);
    SPDLOG_INFO("[LanHostServer] Listening on LAN port {}", port);
    return true;
}

void LanHostServer::Stop() {
    isRunning = false;
    if (serverThread.joinable()) {
        serverThread.join();
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    for (auto& [id, conn] : clients) {
        if (conn.socket) {
            SDLNet_TCP_Close(conn.socket);
        }
    }
    clients.clear();

    if (listenSocket) {
        SDLNet_TCP_Close(listenSocket);
        listenSocket = nullptr;
    }
}

std::string LanHostServer::GetLastError() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return lastError;
}

size_t LanHostServer::GetConnectedClientCount() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return clients.size();
}

std::string LanHostServer::GetLocalLanIp() {
    // Use a non-connecting UDP socket to determine the preferred outbound LAN interface IP.
    // No packets are actually sent over the network.
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        return "127.0.0.1";
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return "127.0.0.1";
    }
#endif

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

    std::string result = "127.0.0.1";
    if (connect(sock, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN] = {};
            if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf))) {
                result = buf;
            }
        }
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return result;
}

void LanHostServer::ServerLoop() {
    SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(static_cast<int>(MAX_LAN_CLIENTS + 2));
    if (!socketSet) {
        SPDLOG_ERROR("[LanHostServer] Failed to allocate socket set: {}", SDLNet_GetError());
        isRunning = false;
        return;
    }

    while (isRunning) {
        // Rebuild socket set each tick (tiny: max 5 sockets)
        SDLNet_FreeSocketSet(socketSet);
        socketSet = SDLNet_AllocSocketSet(static_cast<int>(MAX_LAN_CLIENTS + 2));
        if (!socketSet) {
            break;
        }

        if (listenSocket) {
            SDLNet_TCP_AddSocket(socketSet, listenSocket);
        }

        std::vector<uint32_t> activeIds;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (auto& [id, conn] : clients) {
                if (conn.socket) {
                    SDLNet_TCP_AddSocket(socketSet, conn.socket);
                    activeIds.push_back(id);
                }
            }
        }

        int ready = SDLNet_CheckSockets(socketSet, 10);
        if (ready == -1) {
            SPDLOG_ERROR("[LanHostServer] SDLNet_CheckSockets error: {}", SDLNet_GetError());
            break;
        }

        if (ready > 0) {
            if (listenSocket && SDLNet_SocketReady(listenSocket)) {
                AcceptNewClients();
            }

            for (uint32_t id : activeIds) {
                TCPsocket sock = nullptr;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    auto it = clients.find(id);
                    if (it != clients.end()) {
                        sock = it->second.socket;
                    }
                }
                if (sock && SDLNet_SocketReady(sock)) {
                    ProcessClientData(id);
                }
            }
        }
    }

    if (socketSet) {
        SDLNet_FreeSocketSet(socketSet);
    }
}

void LanHostServer::AcceptNewClients() {
    TCPsocket newSock = SDLNet_TCP_Accept(listenSocket);
    if (!newSock) {
        return;
    }

    IPaddress* peer = SDLNet_TCP_GetPeerAddress(newSock);
    uint32_t peerIpHostOrder = peer ? SDLNet_Read32(&peer->host) : 0;
    bool isLoopback = (peerIpHostOrder == 0x7F000001); // 127.0.0.1

    std::lock_guard<std::mutex> lock(stateMutex);

    if (clients.size() >= MAX_LAN_CLIENTS && !isLoopback) {
        SPDLOG_WARN("[LanHostServer] Max LAN clients reached, rejecting connection");
        SDLNet_TCP_Close(newSock);
        return;
    }

    uint32_t assignedId = 0;
    if (isLoopback) {
        assignedId = 1;
        // Replace any stale host connection
        auto it = clients.find(1);
        if (it != clients.end() && it->second.socket) {
            SDLNet_TCP_Close(it->second.socket);
            clients.erase(it);
        }
    } else {
        assignedId = nextClientId++;
    }

    ClientConnection conn;
    conn.clientId = assignedId;
    conn.socket = newSock;
    clients[assignedId] = std::move(conn);

    SPDLOG_INFO("[LanHostServer] Accepted {} as clientId={}", isLoopback ? "Local Host" : "LAN Client", assignedId);
}

void LanHostServer::ProcessClientData(uint32_t clientId) {
    char buf[4096];
    TCPsocket sock = nullptr;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = clients.find(clientId);
        if (it == clients.end() || !it->second.socket) {
            return;
        }
        sock = it->second.socket;
    }

    int len = SDLNet_TCP_Recv(sock, buf, sizeof(buf));
    if (len <= 0) {
        DisconnectClient(clientId);
        return;
    }

    std::vector<std::string> completePackets;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = clients.find(clientId);
        if (it == clients.end()) {
            return;
        }
        it->second.recvBuffer.append(buf, static_cast<size_t>(len));

        size_t pos = it->second.recvBuffer.find('\0');
        while (pos != std::string::npos) {
            completePackets.push_back(it->second.recvBuffer.substr(0, pos));
            it->second.recvBuffer.erase(0, pos + 1);
            pos = it->second.recvBuffer.find('\0');
        }
    }

    for (const std::string& raw : completePackets) {
        if (raw.empty()) {
            continue;
        }
        try {
            nlohmann::json payload = nlohmann::json::parse(raw);
            HandlePacket(clientId, payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[LanHostServer] Failed to parse packet from client {}: {}", clientId, e.what());
        }
    }
}

void LanHostServer::HandlePacket(uint32_t senderId, nlohmann::json& payload) {
    if (!payload.contains("type") || !payload["type"].is_string()) {
        return;
    }

    std::string type = payload["type"].get<std::string>();
    payload["clientId"] = senderId;

    std::lock_guard<std::mutex> lock(stateMutex);
    auto senderIt = clients.find(senderId);
    if (senderIt == clients.end()) {
        return;
    }

    if (type == "HANDSHAKE") {
        if (payload.contains("clientState") && payload["clientState"].is_object()) {
            senderIt->second.clientState = payload["clientState"];
        }
        senderIt->second.clientState["clientId"] = senderId;
        senderIt->second.clientState["online"] = true;
        senderIt->second.handshakeComplete = true;

        // Host (clientId == 1) always defines room settings & owns the room
        if (senderId == 1 && payload.contains("roomState") && payload["roomState"].is_object()) {
            roomState = payload["roomState"];
            roomState["ownerClientId"] = 1;
        }

        // Send room state to joining client
        nlohmann::json roomPacket = {
            { "type", "UPDATE_ROOM_STATE" },
            { "state", roomState },
        };
        SendJsonToClient(senderIt->second.socket, roomPacket);

        // Broadcast full client list to everyone
        BroadcastAllClientState();
        return;
    }

    if (type == "UPDATE_ROOM_STATE") {
        if (senderId != 1) {
            return; // Only Host can update room settings
        }
        if (payload.contains("state") && payload["state"].is_object()) {
            roomState = payload["state"];
            roomState["ownerClientId"] = 1;
        }
        nlohmann::json roomPacket = {
            { "type", "UPDATE_ROOM_STATE" },
            { "state", roomState },
        };
        for (auto& [id, conn] : clients) {
            if (conn.socket) {
                SendJsonToClient(conn.socket, roomPacket);
            }
        }
        return;
    }

    if (type == "UPDATE_CLIENT_STATE") {
        if (payload.contains("state") && payload["state"].is_object()) {
            senderIt->second.clientState = payload["state"];
            senderIt->second.clientState["clientId"] = senderId;
            senderIt->second.clientState["online"] = true;
        }
        for (auto& [id, conn] : clients) {
            if (id != senderId && conn.socket) {
                SendJsonToClient(conn.socket, payload);
            }
        }
        return;
    }

    if (type == "REQUEST_TEAM_STATE") {
        // Host-authoritative: always route state requests to Host (clientId == 1)
        if (senderId != 1) {
            auto hostIt = clients.find(1);
            if (hostIt != clients.end() && hostIt->second.socket) {
                SendJsonToClient(hostIt->second.socket, payload);
            }
        }
        return;
    }

    // Targeted packet -> send only to targetClientId
    if (payload.contains("targetClientId") && payload["targetClientId"].is_number_unsigned()) {
        uint32_t targetId = payload["targetClientId"].get<uint32_t>();
        auto targetIt = clients.find(targetId);
        if (targetIt != clients.end() && targetIt->second.socket) {
            SendJsonToClient(targetIt->second.socket, payload);
        }
        return;
    }

    // Default: relay to all other connected clients
    for (auto& [id, conn] : clients) {
        if (id != senderId && conn.socket) {
            SendJsonToClient(conn.socket, payload);
        }
    }
}

void LanHostServer::DisconnectClient(uint32_t clientId) {
    std::lock_guard<std::mutex> lock(stateMutex);
    auto it = clients.find(clientId);
    if (it == clients.end()) {
        return;
    }

    SPDLOG_INFO("[LanHostServer] Client {} disconnected", clientId);
    if (it->second.socket) {
        SDLNet_TCP_Close(it->second.socket);
    }
    clients.erase(it);
    BroadcastAllClientState();
}

void LanHostServer::BroadcastAllClientState() {
    // Caller must hold stateMutex
    for (auto& [recipientId, recipient] : clients) {
        if (!recipient.socket) {
            continue;
        }

        nlohmann::json stateArray = nlohmann::json::array();
        for (auto& [id, conn] : clients) {
            if (!conn.handshakeComplete) {
                continue;
            }
            nlohmann::json entry = conn.clientState;
            entry["clientId"] = id;
            entry["online"] = true;
            entry["self"] = (id == recipientId);
            stateArray.push_back(std::move(entry));
        }

        nlohmann::json packet = {
            { "type", "ALL_CLIENT_STATE" },
            { "state", stateArray },
        };
        SendJsonToClient(recipient.socket, packet);
    }
}

void LanHostServer::SendJsonToClient(TCPsocket sock, const nlohmann::json& payload) {
    if (!sock) {
        return;
    }
    std::string data = payload.dump();
    data.push_back('\0');
    SDLNet_TCP_Send(sock, data.data(), static_cast<int>(data.size()));
}
