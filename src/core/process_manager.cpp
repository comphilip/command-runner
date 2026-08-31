#include "core/process_manager.h"

#include <windows.h>

#include <wil/resource.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <expected>
#include <list>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace command_runner {
namespace {

constexpr ULONG_PTR CONTROL_COMPLETION_KEY = 1;
constexpr DWORD PIPE_BUFFER_SIZE = 8192;
constexpr DWORD WORKER_WAIT_MILLISECONDS = 50;
constexpr std::size_t MAX_LOG_LINES = 1000;
constexpr auto SHUTDOWN_TIMEOUT = std::chrono::seconds(8);

std::wstring win32ErrorMessage(DWORD error) {
    std::array<wchar_t, 512> buffer{};
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        nullptr);
    if (length == 0) {
        return L"Windows error " + std::to_wstring(error);
    }

    std::wstring message(buffer.data(), length);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' ||
            message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring errorMessage(std::wstring_view prefix, DWORD error) {
    return std::wstring(prefix) + L": " + win32ErrorMessage(error);
}

std::expected<UINT, std::wstring> outputCodePage(std::string_view encoding) {
    std::string normalized;
    normalized.reserve(encoding.size());
    for (const char value : encoding) {
        normalized.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(value))));
    }

    UINT codePage = CP_ACP;
    if (normalized == "auto" || normalized == "system") {
        codePage = GetACP();
    } else if (normalized == "gbk") {
        codePage = 936;
    } else if (normalized == "utf-8") {
        codePage = CP_UTF8;
    } else {
        return std::unexpected(L"Unsupported output encoding: " +
                                std::wstring(encoding.begin(), encoding.end()));
    }

    if (codePage == 0 || !IsValidCodePage(codePage)) {
        return std::unexpected(L"Unsupported output code page: " +
                                std::to_wstring(codePage));
    }
    return codePage;
}

std::expected<void, std::wstring> verifyWorkingDirectory(
    std::wstring_view workingDirectory) {
    if (workingDirectory.empty()) {
        return {};
    }

    const DWORD attributes = GetFileAttributesW(
        std::wstring(workingDirectory).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return std::unexpected(errorMessage(
            L"Working directory is unavailable", GetLastError()));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return std::unexpected(L"Working directory is not a directory");
    }
    return {};
}

std::expected<std::wstring, std::wstring> commandInterpreter() {
    const DWORD required = GetEnvironmentVariableW(L"COMSPEC", nullptr, 0);
    if (required != 0) {
        std::wstring value(static_cast<std::size_t>(required), L'\0');
        const DWORD written = GetEnvironmentVariableW(
            L"COMSPEC", value.data(), required);
        if (written != 0 && written < required) {
            value.resize(written);
            return value;
        }
    }

    std::array<wchar_t, MAX_PATH> systemDirectory{};
    const UINT length = GetSystemDirectoryW(
        systemDirectory.data(), static_cast<UINT>(systemDirectory.size()));
    if (length == 0 || length >= systemDirectory.size()) {
        return std::unexpected(errorMessage(
            L"Unable to locate the command interpreter", GetLastError()));
    }
    return std::wstring(systemDirectory.data(), length) + L"\\cmd.exe";
}

std::wstring buildCreateProcessCommandLine(const CommandConfig& config) {
    const std::wstring normalized =
        ProcessManager::normalizeCommandLine(config.mCommandLine);
    if (!config.mShell) {
        return normalized;
    }

    const auto interpreter = commandInterpreter();
    if (!interpreter) {
        return normalized;
    }

    // /S /C removes only the command wrapper quotes. The normalized command
    // itself remains intact for cmd.exe, including metacharacters and quotes.
    return L"\"" + *interpreter + L"\" /D /S /C \"" + normalized + L"\"";
}

class IncrementalDecoder final {
public:
    explicit IncrementalDecoder(UINT codePage) : mCodePage(codePage) {}

    [[nodiscard]] std::wstring decode(
        std::span<const std::uint8_t> bytes) {
        mPending.insert(mPending.end(), bytes.begin(), bytes.end());
        return decodePending(false);
    }

    [[nodiscard]] std::wstring finish() {
        return decodePending(true);
    }

private:
    enum class Utf8Result {
        COMPLETE,
        INCOMPLETE,
        INVALID,
    };

    static Utf8Result decodeUtf8CodePoint(
        std::span<const std::uint8_t> bytes,
        std::uint32_t& codePoint,
        std::size_t& consumed) {
        const std::uint8_t first = bytes.front();
        std::size_t length = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (first <= 0x7F) {
            length = 1;
            value = first;
            minimum = 0;
        } else if (first >= 0xC2 && first <= 0xDF) {
            length = 2;
            value = first & 0x1F;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            length = 3;
            value = first & 0x0F;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            length = 4;
            value = first & 0x07;
            minimum = 0x10000;
        } else {
            consumed = 1;
            return Utf8Result::INVALID;
        }

        if (bytes.size() < length) {
            consumed = 0;
            return Utf8Result::INCOMPLETE;
        }
        for (std::size_t index = 1; index < length; ++index) {
            if ((bytes[index] & 0xC0) != 0x80) {
                consumed = 1;
                return Utf8Result::INVALID;
            }
            value = (value << 6) | (bytes[index] & 0x3F);
        }

        if (value < minimum || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) {
            consumed = 1;
            return Utf8Result::INVALID;
        }
        codePoint = value;
        consumed = length;
        return Utf8Result::COMPLETE;
    }

    static void appendCodePoint(std::wstring& output,
                                std::uint32_t codePoint) {
        if (codePoint <= 0xFFFF) {
            output.push_back(static_cast<wchar_t>(codePoint));
            return;
        }
        codePoint -= 0x10000;
        output.push_back(static_cast<wchar_t>(0xD800 | (codePoint >> 10)));
        output.push_back(static_cast<wchar_t>(0xDC00 | (codePoint & 0x3FF)));
    }

    [[nodiscard]] std::wstring decodePending(bool final) {
        std::wstring output;
        while (!mPending.empty()) {
            if (mCodePage == CP_UTF8) {
                std::uint32_t codePoint = 0;
                std::size_t consumed = 0;
                const auto result = decodeUtf8CodePoint(
                    std::span<const std::uint8_t>(mPending.data(),
                                                  mPending.size()),
                    codePoint,
                    consumed);
                if (result == Utf8Result::INCOMPLETE && !final) {
                    break;
                }
                if (result != Utf8Result::COMPLETE) {
                    output.push_back(L'\uFFFD');
                    mPending.erase(mPending.begin());
                    continue;
                }
                appendCodePoint(output, codePoint);
                mPending.erase(mPending.begin(),
                               mPending.begin() +
                                   static_cast<std::ptrdiff_t>(consumed));
                continue;
            }

            std::size_t byteCount = 1;
            if (IsDBCSLeadByteEx(mCodePage, mPending.front())) {
                byteCount = 2;
                if (mPending.size() < byteCount && !final) {
                    break;
                }
            }
            if (mPending.size() < byteCount) {
                output.push_back(L'\uFFFD');
                mPending.erase(mPending.begin());
                continue;
            }

            std::array<char, 2> input{};
            for (std::size_t index = 0; index < byteCount; ++index) {
                input[index] = static_cast<char>(mPending[index]);
            }
            std::array<wchar_t, 2> converted{};
            const int convertedLength = MultiByteToWideChar(
                mCodePage,
                0,
                input.data(),
                static_cast<int>(byteCount),
                converted.data(),
                static_cast<int>(converted.size()));
            if (convertedLength <= 0) {
                output.push_back(L'\uFFFD');
                mPending.erase(mPending.begin());
                continue;
            }
            output.append(converted.data(),
                          static_cast<std::size_t>(convertedLength));
            mPending.erase(mPending.begin(),
                           mPending.begin() +
                               static_cast<std::ptrdiff_t>(byteCount));
        }
        return output;
    }

    UINT mCodePage;
    std::vector<std::uint8_t> mPending;
};

}  // namespace

struct ProcessManager::Impl {
    struct Runtime {
        State mState{State::STOPPED};
        std::optional<std::uint32_t> mPid;
        std::optional<std::int32_t> mExitCode;
        std::uint64_t mGeneration{0};
        std::deque<LogLine> mStdout;
        std::deque<LogLine> mStderr;
        std::deque<LogLine> mCombined;
        std::uint64_t mClearedThrough{0};
    };

    struct ChildProcess {
        wil::unique_handle mProcess;
        wil::unique_handle mThread;
        wil::unique_handle mJob;
        wil::unique_handle mStdoutRead;
        wil::unique_handle mStderrRead;
    };

    enum class StopStage {
        NONE,
        GRACEFUL,
        FORCED,
        FINAL_TERMINATE,
    };

    struct ProcessContext;

    struct PipeContext {
        PipeContext(std::wstring commandId,
                    std::uint64_t generation,
                    std::string stream,
                    UINT codePage)
            : mCommandId(std::move(commandId)),
              mGeneration(generation),
              mStream(std::move(stream)),
              mDecoder(codePage) {}

        std::wstring mCommandId;
        std::uint64_t mGeneration;
        std::string mStream;
        HANDLE mReadHandle{};
        OVERLAPPED mOverlapped{};
        std::array<std::uint8_t, PIPE_BUFFER_SIZE> mBuffer{};
        IncrementalDecoder mDecoder;
        std::wstring mLineBuffer;
        bool mReadPending{false};
        bool mEnded{false};
    };

    struct ProcessContext {
        ProcessContext(std::wstring commandId,
                       std::uint64_t generation,
                       UINT codePage)
            : mCommandId(std::move(commandId)),
              mGeneration(generation),
              mStdout(std::make_unique<PipeContext>(
                  mCommandId, mGeneration, "stdout", codePage)),
              mStderr(std::make_unique<PipeContext>(
                  mCommandId, mGeneration, "stderr", codePage)) {}

        std::wstring mCommandId;
        std::uint64_t mGeneration;
        DWORD mPid{};
        ChildProcess mChild;
        std::unique_ptr<PipeContext> mStdout;
        std::unique_ptr<PipeContext> mStderr;
        StopStage mStopStage{StopStage::NONE};
        std::chrono::steady_clock::time_point mStopDeadline{};
        std::chrono::milliseconds mStopTimeout{0};
        bool mStopRequested{false};
        bool mProcessExited{false};
        DWORD mExitCode{STILL_ACTIVE};
        bool mCancelIssued{false};
        std::optional<CommandConfig> mRestartConfig;
        std::wstring mJobWarning;
    };

    struct PipePair {
        wil::unique_handle mRead;
        wil::unique_handle mWrite;
    };

    struct StartOperation {
        CommandConfig mConfig;
        std::uint64_t mGeneration;
    };

    struct StopOperation {
        std::wstring mCommandId;
        std::uint64_t mGeneration;
        std::chrono::milliseconds mTimeout;
        std::optional<CommandConfig> mRestartConfig;
    };

    struct ShutdownOperation {};

    using Operation = std::variant<StartOperation,
                                   StopOperation,
                                   ShutdownOperation>;

    Impl() {
        mIoCompletionPort.reset(CreateIoCompletionPort(
            INVALID_HANDLE_VALUE, nullptr, 0, 1));
        if (!mIoCompletionPort) {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "CreateIoCompletionPort failed");
        }
        mWorker = std::thread([this] { workerLoop(); });
    }

    ~Impl() {
        close();
    }

    void requestStart(const CommandConfig& config) {
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(mMutex);
            if (mClosing) {
                return;
            }
            Runtime& runtime = mRuntimes[config.mId];
            if (runtime.mState == State::STARTING ||
                runtime.mState == State::RUNNING ||
                runtime.mState == State::STOPPING) {
                return;
            }
            runtime.mState = State::STARTING;
            runtime.mExitCode.reset();
            generation = ++runtime.mGeneration;
            mEvents.emplace_back(
                StateChanged{config.mId, generation});
        }
        enqueue(StartOperation{config, generation});
    }

    void requestStop(std::wstring_view commandId,
                     std::chrono::milliseconds timeout) {
        std::uint64_t generation = 0;
        {
            std::scoped_lock lock(mMutex);
            auto found = mRuntimes.find(std::wstring(commandId));
            if (found == mRuntimes.end()) {
                return;
            }
            Runtime& runtime = found->second;
            if (runtime.mState == State::STARTING) {
                ++runtime.mGeneration;
                runtime.mState = State::STOPPED;
                runtime.mExitCode.reset();
                mEvents.emplace_back(
                    StateChanged{std::wstring(commandId), runtime.mGeneration});
                return;
            }
            if (runtime.mState != State::RUNNING) {
                return;
            }
            runtime.mState = State::STOPPING;
            generation = runtime.mGeneration;
            mEvents.emplace_back(
                StateChanged{std::wstring(commandId), generation});
        }
        enqueue(StopOperation{std::wstring(commandId),
                              generation,
                              std::max(timeout, std::chrono::milliseconds(0)),
                              std::nullopt});
    }

    void requestRestart(const CommandConfig& config) {
        std::uint64_t generation = 0;
        bool shouldStart = false;
        {
            std::scoped_lock lock(mMutex);
            if (mClosing) {
                return;
            }
            Runtime& runtime = mRuntimes[config.mId];
            if (runtime.mState == State::STARTING ||
                runtime.mState == State::STOPPING) {
                return;
            }
            if (runtime.mState != State::RUNNING) {
                shouldStart = true;
            } else {
                runtime.mState = State::STOPPING;
                generation = runtime.mGeneration;
                mEvents.emplace_back(
                    StateChanged{config.mId, generation});
            }
        }
        if (shouldStart) {
            requestStart(config);
            return;
        }
        enqueue(StopOperation{config.mId,
                              generation,
                              std::chrono::seconds(4),
                              config});
    }

    [[nodiscard]] RuntimeSnapshot snapshot(
        std::wstring_view commandId) const {
        std::scoped_lock lock(mMutex);
        const auto found = mRuntimes.find(std::wstring(commandId));
        if (found == mRuntimes.end()) {
            return {};
        }
        const Runtime& runtime = found->second;
        return RuntimeSnapshot{
            runtime.mState,
            runtime.mPid,
            runtime.mExitCode,
            {runtime.mStdout.begin(), runtime.mStdout.end()},
            {runtime.mStderr.begin(), runtime.mStderr.end()},
            {runtime.mCombined.begin(), runtime.mCombined.end()},
            runtime.mClearedThrough};
    }

    [[nodiscard]] std::vector<ProcessEvent> drainEvents() {
        std::vector<ProcessEvent> events;
        std::scoped_lock lock(mMutex);
        events.reserve(mEvents.size());
        while (!mEvents.empty()) {
            events.emplace_back(std::move(mEvents.front()));
            mEvents.pop_front();
        }
        return events;
    }

    [[nodiscard]] std::vector<std::wstring> runningIds() const {
        std::vector<std::wstring> ids;
        std::scoped_lock lock(mMutex);
        for (const auto& [commandId, runtime] : mRuntimes) {
            if (runtime.mState == State::STARTING ||
                runtime.mState == State::RUNNING ||
                runtime.mState == State::STOPPING) {
                ids.push_back(commandId);
            }
        }
        return ids;
    }

    void clearLogs(std::wstring_view commandId) {
        std::scoped_lock lock(mMutex);
        const auto found = mRuntimes.find(std::wstring(commandId));
        if (found == mRuntimes.end()) {
            return;
        }
        Runtime& runtime = found->second;
        if (!runtime.mCombined.empty()) {
            runtime.mClearedThrough = runtime.mCombined.back().mSequence;
        }
        runtime.mStdout.clear();
        runtime.mStderr.clear();
        runtime.mCombined.clear();
    }

    void close() {
        bool requestShutdown = false;
        {
            std::scoped_lock lock(mMutex);
            if (!mClosing) {
                mClosing = true;
                requestShutdown = true;
            }
        }
        if (!requestShutdown) {
            if (mWorker.joinable() &&
                std::this_thread::get_id() != mWorker.get_id()) {
                mWorker.join();
            }
            return;
        }

        enqueue(ShutdownOperation{});
        if (std::this_thread::get_id() != mWorker.get_id() &&
            mWorker.joinable()) {
            mWorker.join();
        }
    }

private:
    static std::wstring makePipeName(std::uint64_t sequence) {
        return L"\\\\.\\pipe\\CommandRunner." +
               std::to_wstring(GetCurrentProcessId()) + L"." +
               std::to_wstring(sequence);
    }

    [[nodiscard]] std::expected<PipePair, std::wstring> createPipe(
        std::wstring_view name) {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;

        wil::unique_handle read(CreateNamedPipeW(
            std::wstring(name).c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            0,
            &attributes));
        if (!read) {
            return std::unexpected(errorMessage(
                L"Unable to create output pipe", GetLastError()));
        }

        wil::unique_handle write(CreateFileW(
            std::wstring(name).c_str(),
            GENERIC_WRITE,
            0,
            &attributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (!write) {
            return std::unexpected(errorMessage(
                L"Unable to open output pipe", GetLastError()));
        }

        if (!SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0)) {
            return std::unexpected(errorMessage(
                L"Unable to protect output pipe", GetLastError()));
        }
        OVERLAPPED connection{};
        if (!ConnectNamedPipe(read.get(), &connection)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                DWORD bytesTransferred = 0;
                if (!GetOverlappedResult(read.get(),
                                         &connection,
                                         &bytesTransferred,
                                         TRUE)) {
                    return std::unexpected(errorMessage(
                        L"Unable to connect output pipe", GetLastError()));
                }
            } else if (error != ERROR_PIPE_CONNECTED) {
                return std::unexpected(errorMessage(
                    L"Unable to connect output pipe", error));
            }
        }
        return PipePair{std::move(read), std::move(write)};
    }

    [[nodiscard]] std::expected<wil::unique_handle, std::wstring> createJob(
        std::wstring& warning) {
        wil::unique_handle job(CreateJobObjectW(nullptr, nullptr));
        if (!job) {
            warning = errorMessage(
                L"[Command Runner] Job Object unavailable; using taskkill as a fallback",
                GetLastError());
            return std::unexpected(warning);
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.get(),
                                      JobObjectExtendedLimitInformation,
                                      &limits,
                                      sizeof(limits))) {
            warning = errorMessage(
                L"[Command Runner] Job Object unavailable; using taskkill as a fallback",
                GetLastError());
            return std::unexpected(warning);
        }
        return std::move(job);
    }

    [[nodiscard]] std::expected<std::unique_ptr<ProcessContext>, std::wstring>
    createProcessContext(const CommandConfig& config,
                         std::uint64_t generation) {
        const auto directoryResult =
            verifyWorkingDirectory(config.mWorkingDirectory);
        if (!directoryResult) {
            return std::unexpected(directoryResult.error());
        }
        const auto codePageResult = outputCodePage(config.mEncoding);
        if (!codePageResult) {
            return std::unexpected(codePageResult.error());
        }

        auto context = std::make_unique<ProcessContext>(
            config.mId, generation, *codePageResult);
        auto stdoutPipe = createPipe(
            makePipeName(++mPipeSequence) + L".stdout");
        if (!stdoutPipe) {
            return std::unexpected(stdoutPipe.error());
        }
        auto stderrPipe = createPipe(
            makePipeName(++mPipeSequence) + L".stderr");
        if (!stderrPipe) {
            return std::unexpected(stderrPipe.error());
        }
        context->mChild.mStdoutRead = std::move(stdoutPipe->mRead);
        context->mChild.mStderrRead = std::move(stderrPipe->mRead);
        context->mStdout->mReadHandle = context->mChild.mStdoutRead.get();
        context->mStderr->mReadHandle = context->mChild.mStderrRead.get();

        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        wil::unique_handle standardInput(CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        if (!standardInput) {
            return std::unexpected(errorMessage(
                L"Unable to open child standard input", GetLastError()));
        }

        auto jobResult = createJob(context->mJobWarning);
        if (jobResult) {
            context->mChild.mJob = std::move(*jobResult);
        }

        // The parent owns the read handles. The child receives only the two
        // pipe write handles and NUL as stdin through the explicit list below.
        const auto stdoutWrite = stdoutPipe->mWrite.get();
        const auto stderrWrite = stderrPipe->mWrite.get();
        std::array<HANDLE, 3> inheritedHandles{
            stdoutWrite, stderrWrite, standardInput.get()};

        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
        if (attributeSize == 0) {
            return std::unexpected(errorMessage(
                L"Unable to prepare child handle list", GetLastError()));
        }
        std::vector<std::byte> attributeStorage(attributeSize);
        auto* attributeList =
            reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                attributeStorage.data());
        if (!InitializeProcThreadAttributeList(
                attributeList, 1, 0, &attributeSize)) {
            return std::unexpected(errorMessage(
                L"Unable to prepare child handle list", GetLastError()));
        }
        const auto deleteAttributeList = [&] {
            DeleteProcThreadAttributeList(attributeList);
        };
        if (!UpdateProcThreadAttribute(attributeList,
                                        0,
                                        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                        inheritedHandles.data(),
                                        sizeof(inheritedHandles),
                                        nullptr,
                                        nullptr)) {
            const DWORD error = GetLastError();
            deleteAttributeList();
            return std::unexpected(errorMessage(
                L"Unable to configure child handle list", error));
        }

        STARTUPINFOEXW startupInfo{};
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.StartupInfo.dwFlags =
            STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startupInfo.StartupInfo.wShowWindow = SW_HIDE;
        startupInfo.StartupInfo.hStdInput = standardInput.get();
        startupInfo.StartupInfo.hStdOutput = stdoutWrite;
        startupInfo.StartupInfo.hStdError = stderrWrite;
        startupInfo.lpAttributeList = attributeList;

        const auto commandLine = buildCreateProcessCommandLine(config);
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                                commandLine.end());
        mutableCommandLine.push_back(L'\0');
        std::wstring interpreter;
        LPCWSTR applicationName = nullptr;
        if (config.mShell) {
            const auto interpreterResult = commandInterpreter();
            if (!interpreterResult) {
                deleteAttributeList();
                return std::unexpected(interpreterResult.error());
            }
            interpreter = *interpreterResult;
            applicationName = interpreter.c_str();
        }

        PROCESS_INFORMATION processInfo{};
        constexpr DWORD creationFlags = EXTENDED_STARTUPINFO_PRESENT |
                                         CREATE_SUSPENDED |
                                         CREATE_NEW_PROCESS_GROUP |
                                         CREATE_NO_WINDOW |
                                         CREATE_UNICODE_ENVIRONMENT;
        const BOOL created = CreateProcessW(
            applicationName,
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            nullptr,
            config.mWorkingDirectory.empty()
                ? nullptr
                : config.mWorkingDirectory.c_str(),
            &startupInfo.StartupInfo,
            &processInfo);
        const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
        deleteAttributeList();
        if (!created) {
            return std::unexpected(errorMessage(
                L"Unable to start command", createError));
        }

        context->mChild.mProcess.reset(processInfo.hProcess);
        context->mChild.mThread.reset(processInfo.hThread);
        context->mPid = processInfo.dwProcessId;

        if (context->mChild.mJob &&
            !AssignProcessToJobObject(context->mChild.mJob.get(),
                                       context->mChild.mProcess.get())) {
            context->mJobWarning = errorMessage(
                L"[Command Runner] Job Object unavailable; using taskkill as a fallback",
                GetLastError());
            context->mChild.mJob.reset();
        }

        if (CreateIoCompletionPort(context->mChild.mStdoutRead.get(),
                                   mIoCompletionPort.get(),
                                   reinterpret_cast<ULONG_PTR>(
                                       context->mStdout.get()),
                                   1) == nullptr ||
            CreateIoCompletionPort(context->mChild.mStderrRead.get(),
                                   mIoCompletionPort.get(),
                                   reinterpret_cast<ULONG_PTR>(
                                       context->mStderr.get()),
                                   1) == nullptr) {
            const DWORD error = GetLastError();
            TerminateProcess(context->mChild.mProcess.get(), 1);
            WaitForSingleObject(context->mChild.mProcess.get(), 2'000);
            return std::unexpected(errorMessage(
                L"Unable to configure output monitoring", error));
        }

        if (ResumeThread(context->mChild.mThread.get()) ==
            static_cast<DWORD>(-1)) {
            const DWORD error = GetLastError();
            TerminateProcess(context->mChild.mProcess.get(), 1);
            WaitForSingleObject(context->mChild.mProcess.get(), 2'000);
            return std::unexpected(errorMessage(
                L"Unable to resume command", error));
        }
        context->mChild.mThread.reset();
        startRead(*context->mStdout);
        startRead(*context->mStderr);
        return context;
    }

    void startRead(PipeContext& pipe) {
        if (pipe.mEnded || pipe.mReadHandle == nullptr) {
            pipe.mEnded = true;
            return;
        }
        std::memset(&pipe.mOverlapped, 0, sizeof(pipe.mOverlapped));
        pipe.mReadPending = true;
        const BOOL started = ReadFile(pipe.mReadHandle,
                                      pipe.mBuffer.data(),
                                      static_cast<DWORD>(pipe.mBuffer.size()),
                                      nullptr,
                                      &pipe.mOverlapped);
        if (started) {
            return;
        }
        const DWORD error = GetLastError();
        pipe.mReadPending = false;
        if (error == ERROR_IO_PENDING) {
            pipe.mReadPending = true;
            return;
        }
        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
            finishPipe(pipe);
            return;
        }
        appendLog(pipe.mCommandId,
                  pipe.mGeneration,
                  "stderr",
                  L"[Command Runner] Output read failed: " +
                      win32ErrorMessage(error));
        finishPipe(pipe);
    }

    void finishPipe(PipeContext& pipe) {
        if (pipe.mEnded) {
            return;
        }
        const std::wstring remaining = pipe.mDecoder.finish();
        appendDecoded(pipe, remaining);
        if (!pipe.mLineBuffer.empty()) {
            appendLog(pipe.mCommandId,
                      pipe.mGeneration,
                      pipe.mStream,
                      std::move(pipe.mLineBuffer));
            pipe.mLineBuffer.clear();
        }
        pipe.mEnded = true;
    }

    void appendDecoded(PipeContext& pipe, std::wstring_view decoded) {
        pipe.mLineBuffer.append(decoded);
        std::size_t newline = pipe.mLineBuffer.find(L'\n');
        while (newline != std::wstring::npos) {
            std::wstring line = pipe.mLineBuffer.substr(0, newline);
            pipe.mLineBuffer.erase(0, newline + 1);
            appendLog(pipe.mCommandId,
                      pipe.mGeneration,
                      pipe.mStream,
                      std::move(line));
            newline = pipe.mLineBuffer.find(L'\n');
        }
    }

    void handlePipeCompletion(PipeContext& pipe,
                              DWORD bytesTransferred,
                              bool completed,
                              DWORD error) {
        if (pipe.mEnded || !pipe.mReadPending) {
            return;
        }
        pipe.mReadPending = false;
        if (bytesTransferred != 0) {
            const auto decoded = pipe.mDecoder.decode(std::span(
                pipe.mBuffer.data(),
                static_cast<std::size_t>(bytesTransferred)));
            appendDecoded(pipe, decoded);
        }
        if (!completed || bytesTransferred == 0 || error != ERROR_SUCCESS) {
            finishPipe(pipe);
            return;
        }
        startRead(pipe);
    }

    void appendLog(const std::wstring& commandId,
                   std::uint64_t generation,
                   std::string stream,
                   std::wstring text) {
        std::scoped_lock lock(mMutex);
        const auto found = mRuntimes.find(commandId);
        if (found == mRuntimes.end() ||
            found->second.mGeneration != generation) {
            return;
        }
        Runtime& runtime = found->second;
        LogLine line = LogLine::create(mNextSequence++,
                                       std::move(stream),
                                       std::move(text));
        auto appendWithLimit = [](std::deque<LogLine>& lines,
                                  const LogLine& value) {
            if (lines.size() >= MAX_LOG_LINES) {
                lines.pop_front();
            }
            lines.push_back(value);
        };
        if (line.mStream == "stdout") {
            appendWithLimit(runtime.mStdout, line);
        } else {
            appendWithLimit(runtime.mStderr, line);
        }
        appendWithLimit(runtime.mCombined, line);
        mEvents.emplace_back(
            LogAdded{commandId, std::move(line), generation});
    }

    [[nodiscard]] ProcessContext* findProcess(
        std::wstring_view commandId,
        std::uint64_t generation) {
        for (const auto& process : mProcesses) {
            if (process->mCommandId == commandId &&
                process->mGeneration == generation &&
                !process->mProcessExited) {
                return process.get();
            }
        }
        return nullptr;
    }

    static bool sendCtrlBreak(DWORD processGroupId) {
        if (GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processGroupId)) {
            return true;
        }

        // A GUI process normally has no console. Attaching to a console-owned
        // child is enough for ordinary console commands; the hidden temporary
        // console is a best-effort fallback for commands that create one late.
        const bool hadConsole = GetConsoleWindow() != nullptr;
        if (!hadConsole && AttachConsole(processGroupId)) {
            const bool delivered =
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processGroupId);
            FreeConsole();
            if (delivered) {
                return true;
            }
        }
        if (!hadConsole && AllocConsole()) {
            if (const HWND console = GetConsoleWindow(); console != nullptr) {
                ShowWindow(console, SW_HIDE);
            }
            const bool delivered =
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processGroupId);
            FreeConsole();
            if (delivered) {
                return true;
            }
        }
        return false;
    }

    static bool runTaskKill(DWORD processId) {
        std::wstring commandLine = L"taskkill.exe /PID " +
                                   std::to_wstring(processId) + L" /T /F";
        std::vector<wchar_t> mutableCommandLine(commandLine.begin(),
                                                commandLine.end());
        mutableCommandLine.push_back(L'\0');
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION processInfo{};
        if (!CreateProcessW(nullptr,
                            mutableCommandLine.data(),
                            nullptr,
                            nullptr,
                            FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            nullptr,
                            nullptr,
                            &startupInfo,
                            &processInfo)) {
            return false;
        }
        wil::unique_handle process(processInfo.hProcess);
        wil::unique_handle thread(processInfo.hThread);
        const DWORD waitResult = WaitForSingleObject(process.get(), 5'000);
        if (waitResult != WAIT_OBJECT_0) {
            return false;
        }
        DWORD exitCode = 1;
        if (!GetExitCodeProcess(process.get(), &exitCode)) {
            return false;
        }
        return exitCode == 0;
    }

    void forceStop(ProcessContext& process) {
        if (process.mProcessExited || process.mChild.mProcess == nullptr) {
            return;
        }
        if (process.mStopStage == StopStage::FORCED ||
            process.mStopStage == StopStage::FINAL_TERMINATE) {
            return;
        }

        bool terminated = false;
        if (process.mChild.mJob) {
            terminated = TerminateJobObject(process.mChild.mJob.get(), 1) != FALSE;
        }
        if (!terminated) {
            terminated = runTaskKill(process.mPid);
        }
        if (!terminated) {
            // taskkill may report failure when the process exits between the
            // query and the command. The normal process poll decides whether
            // this needs the final direct termination fallback.
            TerminateProcess(process.mChild.mProcess.get(), 1);
        }
        process.mStopStage = StopStage::FORCED;
        process.mStopDeadline = std::chrono::steady_clock::now() +
                                std::max(std::chrono::seconds(1),
                                         std::chrono::duration_cast<
                                             std::chrono::seconds>(
                                                 process.mStopTimeout));
    }

    void failStop(ProcessContext& process) {
        {
            std::scoped_lock lock(mMutex);
            const auto found = mRuntimes.find(process.mCommandId);
            if (found == mRuntimes.end() ||
                found->second.mGeneration != process.mGeneration ||
                found->second.mState != State::STOPPING) {
                return;
            }
            found->second.mState = State::RUNNING;
            mEvents.emplace_back(StateChanged{process.mCommandId,
                                              process.mGeneration});
        }
        appendLog(process.mCommandId,
                  process.mGeneration,
                  "stderr",
                  L"[Command Runner] Unable to stop the command process.");
        process.mStopStage = StopStage::NONE;
        process.mStopRequested = false;
        process.mRestartConfig.reset();
    }

    void finalizeProcess(ProcessContext& process, DWORD exitCode) {
        process.mProcessExited = true;
        process.mExitCode = exitCode;
        process.mChild.mProcess.reset();
        process.mChild.mThread.reset();
        process.mChild.mJob.reset();
        if (mShutdownRequested) {
            cancelPipe(*process.mStdout);
            cancelPipe(*process.mStderr);
        }

        std::optional<CommandConfig> restart;
        {
            std::scoped_lock lock(mMutex);
            const auto found = mRuntimes.find(process.mCommandId);
            if (found == mRuntimes.end() ||
                found->second.mGeneration != process.mGeneration) {
                return;
            }
            Runtime& runtime = found->second;
            runtime.mExitCode = static_cast<std::int32_t>(exitCode);
            runtime.mPid.reset();
            runtime.mState = process.mStopRequested
                                 ? State::STOPPED
                                 : (exitCode == 0 ? State::EXITED
                                                  : State::FAILED);
            mEvents.emplace_back(StateChanged{process.mCommandId,
                                              process.mGeneration});
            if (process.mStopRequested && process.mRestartConfig &&
                !mClosing) {
                restart = std::move(process.mRestartConfig);
            }
        }
        if (restart) {
            requestStart(*restart);
        }
    }

    void cancelPipe(PipeContext& pipe) {
        if (pipe.mReadPending && pipe.mReadHandle != nullptr) {
            CancelIoEx(pipe.mReadHandle, &pipe.mOverlapped);
        }
    }

    void pollProcesses() {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& process : mProcesses) {
            if (process->mProcessExited) {
                continue;
            }
            if (!process->mChild.mProcess) {
                finalizeProcess(*process, 1);
                continue;
            }
            const DWORD waitResult =
                WaitForSingleObject(process->mChild.mProcess.get(), 0);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD exitCode = 1;
                if (!GetExitCodeProcess(process->mChild.mProcess.get(),
                                        &exitCode)) {
                    exitCode = 1;
                }
                finalizeProcess(*process, exitCode);
                continue;
            }
            if (waitResult == WAIT_FAILED) {
                if (process->mStopRequested) {
                    failStop(*process);
                } else {
                    finalizeProcess(*process, 1);
                }
                continue;
            }

            if (process->mStopStage == StopStage::GRACEFUL &&
                now >= process->mStopDeadline) {
                forceStop(*process);
            } else if (process->mStopStage == StopStage::FORCED &&
                       now >= process->mStopDeadline) {
                if (TerminateProcess(process->mChild.mProcess.get(), 1)) {
                    process->mStopStage = StopStage::FINAL_TERMINATE;
                    process->mStopDeadline = now + std::chrono::seconds(2);
                } else {
                    failStop(*process);
                }
            } else if (process->mStopStage == StopStage::FINAL_TERMINATE &&
                       now >= process->mStopDeadline) {
                failStop(*process);
            }
        }
    }

    void cleanupProcesses() {
        for (auto iterator = mProcesses.begin(); iterator != mProcesses.end();) {
            const ProcessContext& process = **iterator;
            if (process.mProcessExited && process.mStdout->mEnded &&
                process.mStderr->mEnded) {
                iterator = mProcesses.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void processStart(StartOperation operation) {
        {
            std::scoped_lock lock(mMutex);
            const auto found = mRuntimes.find(operation.mConfig.mId);
            if (mClosing || found == mRuntimes.end() ||
                found->second.mGeneration != operation.mGeneration ||
                found->second.mState != State::STARTING) {
                return;
            }
        }

        auto contextResult = createProcessContext(operation.mConfig,
                                                  operation.mGeneration);
        if (!contextResult) {
            appendLog(operation.mConfig.mId,
                      operation.mGeneration,
                      "stderr",
                      L"[Command Runner] Failed to start: " +
                          contextResult.error());
            std::scoped_lock lock(mMutex);
            const auto found = mRuntimes.find(operation.mConfig.mId);
            if (found != mRuntimes.end() &&
                found->second.mGeneration == operation.mGeneration) {
                found->second.mState = State::FAILED;
                found->second.mExitCode = -1;
                mEvents.emplace_back(StateChanged{operation.mConfig.mId,
                                                  operation.mGeneration});
            }
            return;
        }

        std::unique_ptr<ProcessContext> context = std::move(*contextResult);
        ProcessContext* contextPointer = context.get();
        mProcesses.push_back(std::move(context));

        bool accepted = false;
        {
            std::scoped_lock lock(mMutex);
            const auto found = mRuntimes.find(operation.mConfig.mId);
            if (!mClosing && found != mRuntimes.end() &&
                found->second.mGeneration == operation.mGeneration &&
                found->second.mState == State::STARTING) {
                found->second.mPid = contextPointer->mPid;
                found->second.mState = State::RUNNING;
                mEvents.emplace_back(StateChanged{operation.mConfig.mId,
                                                  operation.mGeneration});
                accepted = true;
            }
        }
        if (accepted) {
            if (!contextPointer->mJobWarning.empty()) {
                appendLog(contextPointer->mCommandId,
                          contextPointer->mGeneration,
                          "stderr",
                          contextPointer->mJobWarning);
            }
            return;
        }

        contextPointer->mStopRequested = true;
        forceStop(*contextPointer);
    }

    void processStop(StopOperation operation) {
        ProcessContext* process = findProcess(operation.mCommandId,
                                              operation.mGeneration);
        if (process == nullptr) {
            return;
        }
        if (operation.mRestartConfig) {
            process->mRestartConfig = std::move(operation.mRestartConfig);
        }
        if (process->mStopStage != StopStage::NONE) {
            return;
        }
        process->mStopRequested = true;
        process->mStopTimeout = operation.mTimeout;
        if (sendCtrlBreak(process->mPid)) {
            process->mStopStage = StopStage::GRACEFUL;
            process->mStopDeadline = std::chrono::steady_clock::now() +
                                     operation.mTimeout;
        } else {
            forceStop(*process);
        }
    }

    void processShutdown() {
        if (mShutdownRequested) {
            return;
        }
        mShutdownRequested = true;
        mShutdownDeadline = std::chrono::steady_clock::now() +
                            SHUTDOWN_TIMEOUT;
        for (const auto& process : mProcesses) {
            if (!process->mProcessExited) {
                process->mStopRequested = true;
                process->mStopTimeout = std::chrono::seconds(1);
                process->mRestartConfig.reset();
                forceStop(*process);
            }
        }
    }

    void processOperations() {
        std::deque<Operation> operations;
        {
            std::scoped_lock lock(mOperationMutex);
            operations.swap(mOperations);
        }
        while (!operations.empty()) {
            Operation operation = std::move(operations.front());
            operations.pop_front();
            std::visit(
                [this](auto&& value) {
                    using OperationType = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<OperationType,
                                                 StartOperation>) {
                        processStart(std::move(value));
                    } else if constexpr (std::is_same_v<OperationType,
                                                        StopOperation>) {
                        processStop(std::move(value));
                    } else {
                        processShutdown();
                    }
                },
                std::move(operation));
        }
    }

    void workerLoop() {
        while (true) {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            OVERLAPPED* overlapped = nullptr;
            const BOOL completed = GetQueuedCompletionStatus(
                mIoCompletionPort.get(),
                &bytesTransferred,
                &completionKey,
                &overlapped,
                WORKER_WAIT_MILLISECONDS);
            if (completionKey == CONTROL_COMPLETION_KEY) {
                processOperations();
            } else if (completionKey != 0 && overlapped != nullptr) {
                auto* pipe = reinterpret_cast<PipeContext*>(completionKey);
                const DWORD error = completed ? ERROR_SUCCESS : GetLastError();
                handlePipeCompletion(*pipe,
                                     bytesTransferred,
                                     completed != FALSE,
                                     error);
            }

            pollProcesses();
            if (mShutdownRequested) {
                const auto now = std::chrono::steady_clock::now();
                if (!mCancelIssued &&
                    (now >= mShutdownDeadline || allProcessesExited())) {
                    mCancelIssued = true;
                    for (const auto& process : mProcesses) {
                        cancelPipe(*process->mStdout);
                        cancelPipe(*process->mStderr);
                    }
                }
            }
            cleanupProcesses();
            if (mShutdownRequested && mProcesses.empty()) {
                return;
            }
        }
    }

    [[nodiscard]] bool allProcessesExited() const {
        return std::all_of(mProcesses.begin(),
                           mProcesses.end(),
                           [](const auto& process) {
                               return process->mProcessExited;
                           });
    }

    void enqueue(Operation operation) {
        {
            std::scoped_lock lock(mOperationMutex);
            mOperations.emplace_back(std::move(operation));
        }
        PostQueuedCompletionStatus(mIoCompletionPort.get(),
                                   0,
                                   CONTROL_COMPLETION_KEY,
                                   nullptr);
    }

    mutable std::mutex mMutex;
    std::unordered_map<std::wstring, Runtime> mRuntimes;
    std::deque<ProcessEvent> mEvents;
    std::uint64_t mNextSequence{1};
    bool mClosing{false};

    std::mutex mOperationMutex;
    std::deque<Operation> mOperations;
    wil::unique_handle mIoCompletionPort;
    std::thread mWorker;
    std::list<std::unique_ptr<ProcessContext>> mProcesses;
    std::uint64_t mPipeSequence{0};
    bool mShutdownRequested{false};
    bool mCancelIssued{false};
    std::chrono::steady_clock::time_point mShutdownDeadline{};
};

ProcessManager::ProcessManager() : mImpl(std::make_unique<Impl>()) {}

ProcessManager::~ProcessManager() = default;

void ProcessManager::start(const CommandConfig& config) {
    mImpl->requestStart(config);
}

void ProcessManager::stop(std::wstring_view commandId,
                          std::chrono::milliseconds timeout) {
    mImpl->requestStop(commandId, timeout);
}

void ProcessManager::restart(const CommandConfig& config) {
    mImpl->requestRestart(config);
}

RuntimeSnapshot ProcessManager::snapshot(std::wstring_view commandId) const {
    return mImpl->snapshot(commandId);
}

std::vector<ProcessEvent> ProcessManager::drainEvents() {
    return mImpl->drainEvents();
}

std::vector<std::wstring> ProcessManager::runningIds() const {
    return mImpl->runningIds();
}

void ProcessManager::clearLogs(std::wstring_view commandId) {
    mImpl->clearLogs(commandId);
}

void ProcessManager::close() {
    if (mImpl) {
        mImpl->close();
    }
}

std::wstring ProcessManager::normalizeCommandLine(
    std::wstring_view commandLine) {
    std::wstring normalized;
    normalized.reserve(commandLine.size());
    for (std::size_t index = 0; index < commandLine.size(); ++index) {
        const wchar_t value = commandLine[index];
        if (value == L'\r') {
            if (index + 1 < commandLine.size() &&
                commandLine[index + 1] == L'\n') {
                ++index;
            }
            normalized.push_back(L' ');
        } else if (value == L'\n') {
            normalized.push_back(L' ');
        } else {
            normalized.push_back(value);
        }
    }
    return normalized;
}

}  // namespace command_runner
