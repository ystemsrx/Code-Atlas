#include "codeexecutor.h"
#include "utils.h"
#include <algorithm>

// 执行器
bool PythonExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".py", "python", "");
}

bool BatchExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".bat", "cmd.exe", "/C");
}

bool PowerShellExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".ps1", "powershell", "-ExecutionPolicy Bypass -File");
}

bool ShellExecutor::Execute(const CodeBlock& block, std::string& resultMessage) {
    return ExecuteCodeWithInterpreter(block, resultMessage, ".sh", "bash", "");
}

// ExecutorFactory
CodeExecutor* ExecutorFactory::CreateExecutor(const std::string& language) {
    std::string lang = language;
    std::transform(lang.begin(), lang.end(), lang.begin(), ::tolower);
    if (lang == "python" || lang == "py") {
        return new PythonExecutor();
    }
    else if (lang == "batch" || lang == "bat" || lang == "cmd") {
        return new BatchExecutor();
    }
    else if (lang == "powershell" || lang == "ps1") {
        return new PowerShellExecutor();
    }
    else if (lang == "shell" || lang == "bash" || lang == "sh") {
        return new ShellExecutor();
    }
    else {
        return nullptr;
    }
}

// ExecuteCode
bool ExecuteCode(const CodeBlock& block, std::string& resultMessage) {
    std::unique_ptr<CodeExecutor> executor(ExecutorFactory::CreateExecutor(block.language));
    if (executor) {
        return executor->Execute(block, resultMessage);
    }
    else {
        resultMessage = "不支持的语言: " + block.language;
        return false;
    }
}
