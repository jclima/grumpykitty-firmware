#include "../../src/app/app_preferences.h"
#include <cassert>
#include <iostream>

int main() {
    AppPreferences prefs;
    prefs.toggle("radio");
    assert(!prefs.visible("radio"));
    assert(prefs.visible("radio-scanner"));
    prefs.toggle("firmware-update");
    assert(prefs.visible("firmware-update"));
    assert(prefs.load(R"({"hidden":["radio","radio"],"order":["removed","b","b","a"]})"));
    assert(prefs.hidden.size() == 1);
    assert((prefs.sorted({"a","b","new"}) == std::vector<std::string>{"b","a","new"}));
    auto before = prefs.encode();
    assert(!prefs.load("{broken"));
    assert(!prefs.load(R"({"hidden":[42],"order":[]})"));
    assert(prefs.encode() == before);
    AppPreferences restored;
    assert(restored.load(before.c_str()));
    assert(restored.encode() == before);
    assert(restored.sorted({}).empty());
    restored.toggle("radio");
    assert(restored.visible("radio"));
    assert(restored.load(R"({"hidden":["firmware-update"],"order":[]})"));
    assert(restored.visible("firmware-update"));
    std::cout << "app preferences: PASS\n";
}
