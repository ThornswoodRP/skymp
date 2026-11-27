#include <memory>
#include <thread>

#include <catch2/catch_all.hpp>
#include <prometheus/core.h>

#include "Networking.h"

using namespace std::chrono_literals;

TEST_CASE("Handler destroys the client", "[Networking]")
{
  auto server =
    Networking::CreateServer("127.0.0.1", 7778, MAX_PLAYERS, "password",
                             std::make_shared<prometheus::Registry>());
  static auto client =
    Networking::CreateClient("127.0.0.1", 7778, 500, "password");

  static bool reset = false;

  for (int i = 0; i < 100; ++i) {
    std::this_thread::sleep_for(10ms);
    server->Tick([](auto, auto, auto, auto, auto) {}, nullptr);

    if (client)
      client->Tick(
        [](auto, auto, auto, auto, auto) {
          client.reset();
          reset = true;
        },
        nullptr);
  }

  REQUIRE(reset);
}

TEST_CASE("Connect/disconnect", "[Networking]")
{
  auto server =
    Networking::CreateServer("127.0.0.1", 7778, MAX_PLAYERS, "password",
                             std::make_shared<prometheus::Registry>());
  auto client = Networking::CreateClient("127.0.0.1", 7778, 500, "password");

  REQUIRE(!client->IsConnected());
  for (int i = 0; i < 100; ++i) {
    std::this_thread::sleep_for(1ms);
    server->Tick([](auto, auto, auto, auto, auto) {}, nullptr);
    client->Tick([](auto, auto, auto, auto, auto) {}, nullptr);
  }
  REQUIRE(client->IsConnected());

  server.reset();

  std::this_thread::sleep_for(500ms * 1.5);
  client->Tick([](auto, auto, auto, auto, auto) {}, nullptr);
  REQUIRE(!client->IsConnected());
}

// THORNSWOOD (#6724). A client that goes tells the server, so the server lets
// the user go at once and not at the end of its 60 s timeout.
TEST_CASE("A destroyed client tells the server it left", "[Networking]")
{
  auto server =
    Networking::CreateServer("127.0.0.1", 7778, MAX_PLAYERS, "password",
                             std::make_shared<prometheus::Registry>());
  auto client = Networking::CreateClient("127.0.0.1", 7778, 60000, "password");

  struct Seen
  {
    bool connected = false;
    bool left = false;
  } seen;
  auto onServer = [](void* state, Networking::UserId,
                     Networking::PacketType type, Networking::PacketData,
                     size_t) {
    auto& s = *reinterpret_cast<Seen*>(state);
    if (type == Networking::PacketType::ServerSideUserConnect)
      s.connected = true;
    if (type == Networking::PacketType::ServerSideUserDisconnect)
      s.left = true;
  };

  for (int i = 0; i < 200 && !(seen.connected && client->IsConnected()); ++i) {
    std::this_thread::sleep_for(5ms);
    server->Tick(onServer, &seen);
    client->Tick([](auto, auto, auto, auto, auto) {}, nullptr);
  }
  REQUIRE(seen.connected);
  REQUIRE(client->IsConnected());

  client.reset();

  // A second, far under the 60 s the server would otherwise wait.
  for (int i = 0; i < 100 && !seen.left; ++i) {
    std::this_thread::sleep_for(10ms);
    server->Tick(onServer, &seen);
  }
  REQUIRE(seen.left);
}

TEST_CASE("Ctors", "[Networking]")
{
  auto server =
    Networking::CreateServer("127.0.0.1", 7778, MAX_PLAYERS, "password",
                             std::make_shared<prometheus::Registry>());
  auto client = Networking::CreateClient("127.0.0.1", 7778, 4000, "password");

  try {
    Networking::CreateServer("127.0.0.1", 7778, MAX_PLAYERS, "password",
                             std::make_shared<prometheus::Registry>());
    REQUIRE(false);
  } catch (std::exception& e) {
    REQUIRE(e.what() == std::string("Peer startup failed with code 5"));
  }

  try {
    Networking::CreateClient("cococo", 1, 4000, "password");
    REQUIRE(false);
  } catch (std::exception& e) {
    REQUIRE(e.what() == std::string("Peer connect failed with code 2"));
  }

  try {
    server->Send(322, nullptr, 0, false);
    REQUIRE(false);
  } catch (std::exception& e) {
    REQUIRE(e.what() == std::string("User with id 322 doesn't exist"));
  }
}
