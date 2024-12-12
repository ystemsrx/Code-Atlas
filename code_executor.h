#ifndef CODE_EXECUTOR_H
#define CODE_EXECUTOR_H

#include "global.h"

// 用于存储待执行代码块信息的结构体
struct CodeBlock {
    std::string code;
    std::string language;
};

// 执行代码的抽象类
class CodeExecutor {
public:
    virtual ~CodeExecutor() {}
    virtual bool Execute(const CodeBlock& block, std::string& resultMessage) = 0;
};

// Python执行类
class PythonExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// Batch执行类
class BatchExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// PowerShell执行类
class PowerShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// Shell执行类
class ShellExecutor : public CodeExecutor {
public:
    bool Execute(const CodeBlock& block, std::string& resultMessage) override;
};

// 工厂类，根据语言创建相应的执行器
class ExecutorFactory {
public:
    static CodeExecutor* CreateExecutor(const std::string& language);
};

// 执行代码块的函数
bool ExecuteCode(const CodeBlock& block, std::string& resultMessage);

// 使用给定解释器和参数执行代码块
bool ExecuteCodeWithInterpreter(const CodeBlock& block, std::string& resultMessage,
    const std::string& fileExt, const std::string& interpreter, const std::string& interpreterArgs);

#endif
