#pragma once
#include "Config.h"
#include "NetworkingInterface.h"
#include "RakNet.h"
#include <cstdlib>
#include <memory>
#include <vector>

namespace prometheus {
class Registry;
}

class IdManager;

namespace Networking {

// localAddress is the address the client's own socket is bound to, any
// when it is null or empty (Thornswood #5359: two test bots on one machine
// come from two addresses, as two people do).
std::shared_ptr<IClient> CreateClient(const char* serverIp,
                                      unsigned short serverPort, int timeoutMs,
                                      const char* password,
                                      const char* localAddress = nullptr);

std::shared_ptr<IServer> CreateServer(
  const char* listenAddress, unsigned short port,
  unsigned short maxConnections, const char* password,
  std::shared_ptr<prometheus::Registry> promRegistry);

void HandlePacketClientside(Networking::IClient::OnPacket onPacket,
                            void* state, Packet* packet);

void HandlePacketServerside(Networking::IServer::OnPacket onPacket,
                            void* state, Packet* packet, IdManager& idManager);

// THORNSWOOD (Thornswood #6568). Which of RakNet's two ends of a connection
// this was, in words for a log: the other side said goodbye
// (ID_DISCONNECTION_NOTIFICATION), or it stopped answering and the timeout
// ran out (ID_CONNECTION_LOST). Both were one "disconnect" on either side,
// so a lost connection and a game that left on purpose looked the same.
// Empty for any other packet id.
const char* DescribeDisconnect(unsigned char packetId, bool serverside);
}
