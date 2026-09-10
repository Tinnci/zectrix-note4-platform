#pragma once

#include "zectrix_cli_control.h"
#include "zectrix_cli_log.h"
#include "zectrix_cli_session.h"

namespace zectrix::cli {

const CommandDescriptor* DiagnosticCommands(std::size_t* count);

class DiagnosticExecutor final : public CliExecutor {
public:
    DiagnosticExecutor(PlatformControlDispatcher& dispatcher, LogBuffer& logs,
                       CliBinarySession* binary = nullptr)
        : dispatcher_(dispatcher), logs_(logs), binary_(binary) {}

    ExecuteStatus Execute(const Invocation&, BoundedOutput*) override;
    ExecuteStatus Poll(BoundedOutput*) override;
    void Cancel() override;
    CliBinarySession* BinarySession() override { return binary_; }
    bool streaming() const { return active_ == Handler::kLogFollow; }

private:
    ExecuteStatus Help(const Invocation&, BoundedOutput*);
    ExecuteStatus FormatResult(BoundedOutput*);
    ExecuteStatus PollLog(BoundedOutput*);

    PlatformControlDispatcher& dispatcher_;
    LogBuffer& logs_;
    CliBinarySession* binary_;
    CancellationToken cancellation_;
    ControlTicket ticket_;
    ControlResult result_;
    Handler active_ = Handler::kNone;
    LogLevel log_level_ = LogLevel::kInfo;
    uint32_t reported_drops_ = 0;
    std::size_t page_ = 0;
    bool result_ready_ = false;
};

}  // namespace zectrix::cli
