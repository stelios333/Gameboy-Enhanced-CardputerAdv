// SD card browser for selecting a .gb / .gbc file.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <algorithm>
#include <string>
#include <vector>
#include <strings.h>
#include <stdio.h>
#include <string.h>

#include "fe_ui.h"
#include "fe_state.h"
#include "fe_settings.h"

namespace fe {

// ---------------------------------------------------------------------------
// File picker
// ---------------------------------------------------------------------------
namespace {

bool isRomName(const String& name)
{
    const int dot = name.lastIndexOf('.');
    if (dot < 0) {
        return false;
    }
    String ext = name.substring(dot + 1);
    ext.toLowerCase();
    return ext == "gb" || ext == "gbc";
}

void drawPicker(const String& dir, const std::vector<String>& names,
                const std::vector<bool>& isDir, int selected)
{
    M5Cardputer.Display.clearDisplay();
    M5Cardputer.Display.setTextColor(0xFFFF);
    setFontSize(80);

    M5Cardputer.Display.drawString(" /\\ \\/ TO NAVIGATE | ENTER TO LOAD", 2, 0);
    M5Cardputer.Display.drawString(" PRESS ESC OR ' KEY FOR SETTINGS MENU", 2, 124);

    const int visible = 5;
    int first = selected - visible / 2;
    if (first < 0) first = 0;
    if (first + visible > static_cast<int>(names.size())) {
        first = std::max(0, static_cast<int>(names.size()) - visible);
    }

    for (int i = 0; i < visible; i++) {
        const int index = first + i;
        if (index >= static_cast<int>(names.size())) {
            break;
        }
        const int y = 20 + i * 20;
        if (index == selected) {
            M5Cardputer.Display.fillRect(0, y - 2, 240, 18, 0x8410);
            M5Cardputer.Display.setTextColor(0x0000);
            M5Cardputer.Display.drawString(">", 4, y);
        } else {
            M5Cardputer.Display.setTextColor(0xFFFF);
        }
        String label = names[index];
        if (isDir[index]) {
            label = String("[") + label + "]";
        }
        M5Cardputer.Display.drawString(label.c_str(), 18, y);
    }
}

}  // namespace

void filePicker(char* out, size_t outSize)
{
    String currentDir = "/";
    int selected = 0;

    while (true) {
        std::vector<String> names;
        std::vector<bool> isDir;

        File dir = SD.open(currentDir);
        if (!dir) {
            messageBoxDraw(-15, "SD Card has no", "Directory/Filename", 0xFFFF,
                           0xFFFF, 2, 2, 4);
            delay(1000);
            strncpy(out, "/", outSize);
            return;
        }

        File entry = dir.openNextFile();
        while (entry) {
            String name = entry.name();
            // Some cores return the full path; keep only the leaf name.
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) {
                name = name.substring(slash + 1);
            }

            if (name != "System Volume Information" &&
                names.size() < static_cast<size_t>(kMaxFiles)) {
                if (entry.isDirectory()) {
                    names.push_back(name);
                    isDir.push_back(true);
                } else if (isRomName(name)) {
                    names.push_back(name);
                    isDir.push_back(false);
                }
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();

        // Directories first, then case-insensitive name order.
        std::vector<int> order(names.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (isDir[a] != isDir[b]) return isDir[a] > isDir[b];
            return strcasecmp(names[a].c_str(), names[b].c_str()) < 0;
        });

        std::vector<String> sortedNames;
        std::vector<bool> sortedDirs;
        for (int i : order) {
            sortedNames.push_back(names[i]);
            sortedDirs.push_back(isDir[i]);
        }
        names.swap(sortedNames);
        isDir.swap(sortedDirs);

        // Allow navigating back to the parent directory.
        if (currentDir != "/") {
            names.insert(names.begin(), "..");
            isDir.insert(isDir.begin(), true);
        }

        if (names.empty()) {
            messageBoxDraw(-15, "SD Card has no", ".gb or .gbc files", 0xFFFF,
                           0xFFFF, 2, 2, 4);
            delay(1000);
            strncpy(out, "/", outSize);
            return;
        }

        if (selected < 0) selected = static_cast<int>(names.size()) - 1;
        if (selected >= static_cast<int>(names.size())) selected = 0;

        drawPicker(currentDir, names, isDir, selected);
        waitKeyRelease();

        bool done = false;
        while (!done) {
            M5Cardputer.update();
            if (!M5Cardputer.Keyboard.isPressed()) {
                continue;
            }
            auto& state = M5Cardputer.Keyboard.keysState();

            if (state.enter) {
                if (isDir[selected]) {
                    if (names[selected] == "..") {
                        const int slash = currentDir.lastIndexOf('/');
                        currentDir = (slash <= 0) ? "/" : currentDir.substring(0, slash);
                    } else {
                        if (!currentDir.endsWith("/")) currentDir += "/";
                        currentDir += names[selected];
                    }
                    selected = 0;
                } else {
                    if (currentDir == "/") {
                        snprintf(out, outSize, "/%s", names[selected].c_str());
                    } else {
                        snprintf(out, outSize, "%s/%s", currentDir.c_str(),
                                 names[selected].c_str());
                    }
                    return;
                }
                done = true;
                continue;
            }

            if (state.del || (state.word.size() && state.word[0] == '`')) {
                // Leave the picker and let the caller re-enter it.
                strncpy(out, "", outSize);
                return;
            }

            for (char key : state.word) {
                if (key == ';' || key == 'e') {
                    selected--;
                    delay(kDelayKeyRepeatUs / 1000);
                    done = true;
                    break;
                }
                if (key == '.' || key == 's') {
                    selected++;
                    delay(kDelayKeyRepeatUs / 1000);
                    done = true;
                    break;
                }
            }
        }
    }
}

}  // namespace fe
