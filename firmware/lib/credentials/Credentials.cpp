#include "Credentials.h"
#include <Preferences.h>

static const char *NVS_NAMESPACE = "credentials";
static const char *NVS_KEY_OPENAI = "openai_key";

bool Credentials::hasOpenAIKey()
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    bool found = prefs.isKey(NVS_KEY_OPENAI);
    prefs.end();
    return found;
}

String Credentials::getOpenAIKey()
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String key = prefs.getString(NVS_KEY_OPENAI, "");
    prefs.end();
    return key;
}

void Credentials::setOpenAIKey(const String &key)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false); // read-write
    prefs.putString(NVS_KEY_OPENAI, key);
    prefs.end();
}

void Credentials::clearOpenAIKey()
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(NVS_KEY_OPENAI);
    prefs.end();
}
