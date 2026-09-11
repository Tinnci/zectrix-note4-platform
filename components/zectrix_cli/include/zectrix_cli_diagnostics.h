#pragma once

#include "zectrix_cli_control.h"
#include "zectrix_cli_log.h"
#include "zectrix_cli_session.h"

namespace zectrix::cli {

const CommandDescriptor* DiagnosticCommands(std::size_t* count);

class DiagnosticExecutor final : public CliExecutor {
public:
    DiagnosticExecutor(PlatformControlDispatcher& dispatcher, LogBuffer& logs,
                       CliBinarySession* binary = nullptr, MonotonicMilliseconds clock = SteadyMilliseconds)
        : dispatcher_(dispatcher), logs_(logs), binary_(binary), clock_(clock) {}

    ExecuteStatus Execute(const Invocation&, BoundedOutput*) override;
    ExecuteStatus Poll(BoundedOutput*) override;
    void Cancel() override;
    ExecuteStatus CancelStatus() override;
    CliBinarySession* BinarySession() override { return binary_; }
    bool streaming() const { return active_ == Handler::kLogFollow || active_ == Handler::kInputWatch; }

private:
    ExecuteStatus Help(const Invocation&, BoundedOutput*);
    ExecuteStatus FormatResult(BoundedOutput*);
    ExecuteStatus PollLog(BoundedOutput*);
    ExecuteStatus PollInput(BoundedOutput*);
    ExecuteStatus Submit(Handler handler, const ControlRequest& request);

    PlatformControlDispatcher& dispatcher_;
    LogBuffer& logs_;
    CliBinarySession* binary_;
    MonotonicMilliseconds clock_;
    CancellationToken cancellation_;
    ControlTicket ticket_;
    ControlResult result_;
    Handler active_ = Handler::kNone;
    LogLevel log_level_ = LogLevel::kInfo;
    uint32_t reported_drops_ = 0;
    std::size_t page_ = 0;
    bool result_ready_ = false;
    ControlRequest confirmation_{};
    Handler confirmation_handler_ = Handler::kNone;
    uint64_t confirmation_token_ = 0, next_token_ = 0, confirmation_deadline_ = 0;
    uint64_t input_cursor_ = 0, next_input_poll_ms_ = 0;
};

}  // namespace zectrix::cli
