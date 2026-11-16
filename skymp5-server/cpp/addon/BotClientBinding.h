#pragma once
#include <napi.h>

/*
  THORNSWOOD (Thornswood #5196). createBotClient(host, port, password,
  timeoutMs, localAddress) for Node: a scripted bot's own connection to a
  running server, over the network like a game's (mp_common/BotClient.h). It
  is not the in-process createBot of ScampServer, which never leaves the
  server and reads nothing back. localAddress, any when left out, is the
  address the bot's socket is bound to (#5359): the server refuses a second
  connection from one address within 100 ms of the first, so bots that stand
  for different people each come from a loopback address of their own.

    const bot = addon.createBotClient("127.0.0.1", 7777, "", 60000,
                                      "127.0.1.1");
    bot.tick()         [{ type, message?, raw?, error? }, ...]: type is
                       "message", "connectionAccepted", "connectionFailed",
                       "connectionDenied" or "disconnect". A message the
                       serializer read is in message as an object; a packet
                       it did not know is in raw as its text, which the bot
                       reads as skymp5-client does.
    bot.send(obj, reliable = true)   one message, as skymp5-client sends it.
    bot.isConnected()
    bot.destroy()      closes the connection; the object does nothing after.
*/
namespace BotClientBinding {
void Init(Napi::Env env, Napi::Object exports);
}
