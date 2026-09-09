#include "terminal_internal.h"

namespace zectrix::terminal {

bool TerminalApp::AddApplication(const char* id, const char* label, Creator creator) {
    if (applications_.size() == factories_.size()) return false;
    auto& factory = factories_[applications_.size()];
    factory.owner = this;
    factory.creator = creator;
    return applications_.Add(id, label, factory);
}

bool TerminalApp::ComposeApplications() {
    if (!AddApplication("launcher", "Launcher", CreateLauncher)) return false;
#if CONFIG_ZECTRIX_ENABLE_READER
    if (storage_ && !AddApplication("reader", "BOOK READER", CreateReader)) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER
    if (connectivity_ && storage_ &&
        !AddApplication("book-transfer", "SEND BOOKS", CreateBookTransfer)) return false;
#endif
    if (!AddApplication("clock", "CLOCK", CreateClock) ||
        !AddApplication("sleep-cover", "SLEEP COVER", CreateSleepCover) ||
        !AddApplication("settings", "SETTINGS", CreateSettings)) return false;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    if (connectivity_ &&
        !AddApplication("connectivity", "CONNECTIVITY", CreateConnectivity)) return false;
#endif
    return AddApplication("showcase", "AUTO SHOWCASE", CreateShowcase) &&
           AddApplication("gallery", "DISPLAY GALLERY", CreateGallery) &&
           AddApplication("diagnostics", "HARDWARE TESTS", CreateDiagnostics) &&
           AddApplication("device-info", "DEVICE INFO", CreateDeviceInfo) &&
           AddApplication("about", "ABOUT & LICENSE", CreateAbout);
}

}  // namespace zectrix::terminal
