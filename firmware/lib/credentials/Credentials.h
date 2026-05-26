#ifndef _credentials_h_
#define _credentials_h_

#include <Arduino.h>

class Credentials
{
public:
    static bool hasOpenAIKey();
    static String getOpenAIKey();
    static void setOpenAIKey(const String &key);
    static void clearOpenAIKey();
};

#endif
