// Camelion++ - assorted dialogs
#pragma once
#include <gtk/gtk.h>
#include <string>

namespace cam {
class MainWindow;
class Editor;

std::string inputBox(GtkWindow* parent, const std::string& title, const std::string& prompt,
                     const std::string& initial, bool* ok);
void goToDialog(MainWindow* mw);
void columnEditorDialog(MainWindow* mw);
void preferencesDialog(MainWindow* mw);
void styleConfiguratorDialog(MainWindow* mw);
void summaryDialog(MainWindow* mw);
void aboutDialog(MainWindow* mw);
void cmdLineHelpDialog(MainWindow* mw);
void debugInfoDialog(MainWindow* mw);
bool runMacroMultipleDialog(MainWindow* mw, int& times, bool& untilEnd);
bool runDialog(MainWindow* mw, std::string& command, std::string& saveName);
void printDocument(MainWindow* mw);
std::string expandRunVariables(MainWindow* mw, const std::string& cmd);

} // namespace cam
