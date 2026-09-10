#include "terminal_internal.h"

namespace zectrix::terminal {

bool TerminalApp::AddApplication(const char* id, const char* label, Creator creator,
                                 app::ApplicationPresentation presentation) {
    if (applications_.size() == factories_.size()) return false;
    auto& factory = factories_[applications_.size()];
    factory.owner = this;
    factory.creator = creator;
    return applications_.Add(id, label, factory, presentation);
}

bool TerminalApp::ComposeApplications() {
    using Icon = app::ApplicationIcon;
    if (!AddApplication("launcher", "Launcher", CreateLauncher)) return false;
#if CONFIG_ZECTRIX_ENABLE_READER
    if (storage_ && !AddApplication("reader", "BOOK READER", CreateReader, {Icon::Book, true})) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER
    if (connectivity_ && storage_ &&
        !AddApplication("book-transfer", "SEND BOOKS", CreateBookTransfer, {Icon::Transfer, true})) return false;
#endif
    if (!AddApplication("clock", "CLOCK", CreateClock, {Icon::Clock, true}) ||
        !AddApplication("sleep-cover", "SLEEP COVER", CreateSleepCover, {Icon::Sleep, true}) ||
        !AddApplication("settings", "SETTINGS", CreateSettings, {Icon::Settings, true})) return false;
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
