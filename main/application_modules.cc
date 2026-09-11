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
    using Text = i18n::Text;
    if (!AddApplication("launcher", "Launcher", CreateLauncher)) return false;
#if CONFIG_ZECTRIX_ENABLE_READER
    if (storage_ && !AddApplication("reader", "BOOK READER", CreateReader, {Icon::Book, true, Text::BookReader})) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_BOOK_TRANSFER
    if (connectivity_ && storage_ &&
        !AddApplication("book-transfer", "SEND BOOKS", CreateBookTransfer, {Icon::Transfer, true, Text::SendBooks})) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_RUNTIME
    if (storage_ && !AddApplication("apps", "APPS", CreateMicroApps, {Icon::App, true, Text::Apps})) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_UTILITIES
    if (!AddApplication("utilities", "POCKET TOOLS", CreateUtilities, {Icon::App, true, Text::PocketTools})) return false;
#endif
    if (!AddApplication("clock", "CLOCK", CreateClock, {Icon::Clock, true, Text::Clock}) ||
        !AddApplication("sleep-cover", "SLEEP COVER", CreateSleepCover, {Icon::Sleep, true, Text::SleepCover}) ||
        !AddApplication("settings", "SETTINGS", CreateSettings, {Icon::Settings, true, Text::Settings})) return false;
#if CONFIG_ZECTRIX_ENABLE_CONNECTIVITY
    if (connectivity_ &&
        !AddApplication("connectivity", "CONNECTIVITY", CreateConnectivity, {Icon::App, false, Text::Connectivity})) return false;
#endif
#if CONFIG_ZECTRIX_ENABLE_USB_HOST
    if (usb_host_ && storage_ &&
        !AddApplication("usb-manager", "USB MANAGER", CreateUsbManager, {Icon::Transfer, false, Text::UsbManager})) return false;
#endif
    return AddApplication("showcase", "AUTO SHOWCASE", CreateShowcase, {Icon::App, false, Text::AutoShowcase}) &&
           AddApplication("gallery", "DISPLAY GALLERY", CreateGallery, {Icon::App, false, Text::DisplayGallery}) &&
           AddApplication("diagnostics", "HARDWARE TESTS", CreateDiagnostics, {Icon::App, false, Text::HardwareTests}) &&
           AddApplication("device-info", "DEVICE INFO", CreateDeviceInfo, {Icon::App, false, Text::DeviceInfo}) &&
           AddApplication("about", "ABOUT & LICENSE", CreateAbout, {Icon::App, false, Text::AboutLicense});
}

}  // namespace zectrix::terminal
