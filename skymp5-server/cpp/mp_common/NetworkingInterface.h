#pragma once
#include "MinPacketId.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Networking {

using UserId = unsigned short;

constexpr UserId InvalidUserId = static_cast<UserId>(~0);

// First byte must represent id of the packet (>= MinPacketId)
using PacketData = const unsigned char*;

enum class PacketType
{
  Invalid = -1,
  Message,
  ClientSideDisconnect,
  ClientSideConnectionAccepted,
  ClientSideConnectionFailed,
  ClientSideConnectionDenied,
  ServerSideUserConnect,
  ServerSideUserDisconnect
};

class IClient
{
public:
  using OnPacket = void (*)(void* state, PacketType packetType,
                            PacketData data, size_t length, const char* error);

  virtual ~IClient() = default;

  virtual void Send(PacketData data, size_t length, bool reliable) = 0;
  virtual void Tick(OnPacket onPacket, void* state) = 0;
  virtual bool IsConnected() const = 0;
};

class ISendTarget
{
public:
  virtual ~ISendTarget() = default;

  virtual void Send(UserId targetUserId, PacketData data, size_t length,
                    bool reliable) = 0;
};

// How one connection is doing, as the transport sees it (Thornswood #5053:
// "the dev server lags at the Whiterun market", and nothing on the server said
// what a connection did, only how busy the server was). The byte counts run
// from when the connection opened; a reader takes the difference between two
// reads for a rate. -1 is a ping the transport has not measured yet.
struct ConnectionStats
{
  int lastPingMs = -1;
  int averagePingMs = -1;
  int lowestPingMs = -1;
  uint64_t bytesSent = 0;        // on the wire, resends and headers included
  uint64_t bytesResent = 0;      // of the messages' own bytes, sent again
  uint64_t bytesReceived = 0;    // on the wire
  unsigned messagesWaiting = 0;  // queued to send now, every priority
  unsigned messagesToResend = 0; // sent and not yet acknowledged
  float packetLoss = 0;          // since it opened, 0 to 1
  bool limitedByCongestion = false;
};

class IServer : public ISendTarget
{
public:
  typedef void (*OnPacket)(void* state, UserId userId, PacketType packetType,
                           PacketData data, size_t length);

  virtual ~IServer() = default;

  virtual void Tick(OnPacket onPacket, void* state) = 0;

  virtual std::string GetIp(UserId userId) const = 0;

  virtual ConnectionStats GetConnectionStats(UserId userId) const = 0;

  virtual void CloseConnection(UserId userId) = 0;
};
}
