#include <WiFiNINA.h>
#include "MixedHttpClient.h"

#define LOGGING

MixedHttpClient::MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const char* aServerName, uint16_t aServerPort)
  : HttpClient(aClient, aServerName, aServerPort), iUseSSL(aUseSSL)
{
  resetState();
}

MixedHttpClient::MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const String& aServerName, uint16_t aServerPort)
  : MixedHttpClient(aClient, aUseSSL, aServerName.c_str(), aServerPort)
{
}

MixedHttpClient::MixedHttpClient(WiFiClient& aClient, bool aUseSSL, const IPAddress& aServerAddress, uint16_t aServerPort)
  : HttpClient(aClient, aServerAddress, aServerPort), iUseSSL(aUseSSL)
{
}

int MixedHttpClient::connect(IPAddress ip, uint16_t port) {
  if (iUseSSL)
    return ((WiFiClient *)iClient)->connectSSL(ip, port);
  else
    return iClient->connect(ip, port);
}

int MixedHttpClient::connect(const char *host, uint16_t port) {
  if (iUseSSL)
    return ((WiFiClient *)iClient)->connectSSL(host, port);
  else
    return iClient->connect(host, port);
}

int MixedHttpClient::startRequest(const char* aURLPath, const char* aHttpMethod, 
    const char* aContentType, int aContentLength, const byte aBody[])
{
  Serial.println("prep for request\n");
  if (iState == eReadingBody || iState == eReadingChunkLength || iState == eReadingBodyChunk)
  {
    flushClientRx();

    resetState();
  }
  Serial.println("prep 2\n");

  tHttpState initialState = iState;

  if ((eIdle != iState) && (eRequestStarted != iState))
  {
    return HTTP_ERROR_API;
  }
  Serial.println("prep 3\n");

  if (iConnectionClose || !iClient->connected())
  {
  Serial.println("prep 4\n");
    if (iServerName)
    {
      if (!(connect(iServerName, iServerPort) > 0))
      {
#ifdef LOGGING
        Serial.println("Connection failed");
#endif
        return HTTP_ERROR_CONNECTION_FAILED;
      }
    }
    else
    {
      if (!(connect(iServerAddress, iServerPort) > 0))
      {
#ifdef LOGGING
        Serial.println("Connection failed");
#endif
        return HTTP_ERROR_CONNECTION_FAILED;
      }    
    }
  }
  else
  {
#ifdef LOGGING
    Serial.println("Connection already open");
#endif
  }

  // Now we're connected, send the first part of the request
  Serial.print("aboutt o send initial headers for: ");
  Serial.println(aURLPath);
  int ret = sendInitialHeaders(aURLPath, aHttpMethod);

  if (HTTP_SUCCESS == ret)
  {
    if (aContentType)
    {
      sendHeader(HTTP_HEADER_CONTENT_TYPE, aContentType);
    }

    if (aContentLength > 0)
    {
      sendHeader(HTTP_HEADER_CONTENT_LENGTH, aContentLength);
    }

    bool hasBody = (aBody && aContentLength > 0);

    if (initialState == eIdle || hasBody)
    {
      // This was a simple version of the API, so terminate the headers now
      finishHeaders();
    }
    // else we'll call it in endRequest or in the first call to print, etc.

    if (hasBody)
    {
      write(aBody, aContentLength);
    }
  }

  return ret;
}
