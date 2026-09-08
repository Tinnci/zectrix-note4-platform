#pragma once

#include "zectrix_cli_control.h"
#include "zectrix_cli_log.h"
#include "zectrix_cli_session.h"

namespace zectrix::cli {

const CommandDescriptor* DiagnosticCommands(std::size_t* count);

class DiagnosticExecutor final : public CliExecutor {
public:
    DiagnosticExecutor(PlatformControlDispatcher& dispatcher, LogBuffer& logs)
        : dispatcher_(dispatcher), logs_(logs) {}

    ExecuteStatus Execute(const Invocation&, BoundedOutput*) override;
    ExecuteStatus Poll(BoundedOutput*) override;
    void Cancel() override;

private:
    ExecuteStatus Help(const Invocation&, BoundedOutput*);
    ExecuteStatus FormatResult(BoundedOutput*);
    ExecuteStatus PollLog(BoundedOutput*);

    PlatformControlDispatcher& dispatcher_;
    LogBuffer& logs_;
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
