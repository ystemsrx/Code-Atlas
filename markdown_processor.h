#ifndef MARKDOWN_PROCESSOR_H
#define MARKDOWN_PROCESSOR_H

#include "global.h"

// 定义抽象类，用于处理行字符处理
class LineProcessor {
public:
    virtual ~LineProcessor() {}
    virtual void processChar(char ch) = 0;
    virtual void resetLineState() = 0;
};

// MarkdownElement为抽象类，用于处理Markdown中不同元素的解析
class MarkdownElement {
public:
    virtual ~MarkdownElement() {}
    virtual bool process(char ch, size_t& pos, const std::string& accumulator, HANDLE hConsole, WORD& currentColor) = 0;
    virtual void reset() = 0;
    virtual bool isActive() const = 0;
};

// CodeBlock结构体在code_executor.h中定义，这里先声明
struct CodeBlock; 

// CodeBlockElement类，用于处理Markdown代码块逻辑
class CodeBlockElement : public MarkdownElement {
private:
    bool inCodeBlock = false;
    std::string codeBuffer;
    std::string startBackTickBuffer;
    int startBackTickCount = 0;
    std::string endBackTickBuffer;
    int endBackTickCount = 0;
    bool collectingFirstLine = false;
    std::string firstLineBuffer;
    WORD currentCodeColor = CODE_COLOR;
public:
    bool process(char ch, size_t& pos, const std::string& accumulator, HANDLE hConsole, WORD& currentColor) override;
    void reset() override;
    bool isActive() const override;
};

// MarkdownLineProcessor类，用于处理Markdown行首标题及*、_格式化标记
class MarkdownLineProcessor : public LineProcessor {
public:
    MarkdownLineProcessor(HANDLE hConsole);
    void resetLineState() override;
    void processChar(char ch) override;

private:
    struct MarkdownState {
        bool headingDetected = false;
        int headingLevel = 0;
        bool inBold = false;
        bool inItalic = false;
        WORD baseColor = DEFAULT_COLOR;

        WORD currentColor() const {
            if (inBold) return BOLD_COLOR;
            if (inItalic) return ITALIC_COLOR;
            return baseColor;
        }
    };

    void handleFormattingMarker(char marker);

    HANDLE hConsole;
    MarkdownState state;
    bool lineStart = true;
    bool collectingHash = false;
    int hashCount = 0;
    char lastMarkerChar = '\0';
    bool lastMarkerUsed = true;
};

// 工具函数声明
std::string GetLastNonEmptyLine(const std::string& str);
std::wstring Utf8ToWstring(const std::string& str);
std::string ReadPipeOutput(HANDLE hPipe);

#endif
