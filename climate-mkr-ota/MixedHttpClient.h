#ifndef MIXEDCLIENT_H
#define MIXEDCLIENT_H

#include <WiFiNINA.h>
#include "ArduinoHttpClient.h"

class MixedHttpClient : public HttpClient {
  bool iUseSSL;

public:
  MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const char* aServerName, uint16_t aServerPort = kHttpPort);
  MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const String& aServerName, uint16_t aServerPort = kHttpPort);
  MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const IPAddress& aServerAddress, uint16_t aServerPort = kHttpPort);

  virtual int connect(IPAddress ip, uint16_t port);
  virtual int connect(const char *host, uint16_t port);

  virtual int startRequest(const char* aURLPath, const char* aHttpMethod, 
    const char* aContentType, int aContentLength, const byte aBody[]);
};

#endif /* MIXEDCLIENT_H */
