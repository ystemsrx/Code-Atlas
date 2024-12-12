#include "markdown_processor.h"
#include "code_executor.h" // 因为需要使用 CodeBlock
#include <algorithm>

// 工具函数实现
std::string GetLastNonEmptyLine(const std::string& str) {
    std::istringstream stream(str);
    std::string line, lastLine;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            lastLine = line;
        }
    }
    return lastLine;
}

// 将UTF-8字符串转为宽字符串
std::wstring Utf8ToWstring(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstrTo[0], size_needed);
    return wstrTo;
}

// 从管道中读取输出并转为UTF-8
std::string ReadPipeOutput(HANDLE hPipe) {
    const int bufferSize = 4096;
    char buffer[bufferSize];
    DWORD bytesRead;
    std::wstring wOutput;

    while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        int wchars_num = MultiByteToWideChar(CP_ACP, 0, buffer, bytesRead, NULL, 0);
        std::wstring wstr(wchars_num, 0);
        MultiByteToWideChar(CP_ACP, 0, buffer, bytesRead, &wstr[0], wchars_num);
        wOutput.append(wstr);
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wOutput.c_str(), -1, NULL, 0, NULL, NULL);
    std::string utf8_str(utf8_len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wOutput.c_str(), -1, &utf8_str[0], utf8_len, NULL, NULL);
    return utf8_str;
}

// CodeBlockElement类方法实现
bool CodeBlockElement::process(char ch, size_t& pos, const std::string& accumulator, HANDLE hConsole, WORD& currentColor) {
    if (!inCodeBlock) {
        if (ch == '`') {
            startBackTickBuffer += '`';
            startBackTickCount++;
            pos++;
            if (startBackTickCount == 3) {
                collectingFirstLine = true;
                firstLineBuffer.clear();
                codeBuffer.clear();
                startBackTickBuffer.clear();
                startBackTickCount = 0;
                inCodeBlock = true;
                return true;
            }
            return true;
        } else {
            if (!startBackTickBuffer.empty()) {
                for (char c : startBackTickBuffer) {
                    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                    std::cout << c;
                }
                startBackTickBuffer.clear();
                startBackTickCount = 0;
            }
            return false;
        }
    } else {
        if (collectingFirstLine) {
            if (ch == '\n') {
                collectingFirstLine = false;
                std::string lowerFirstLine = firstLineBuffer;
                std::transform(lowerFirstLine.begin(), lowerFirstLine.end(), lowerFirstLine.begin(), ::tolower);
                currentCodeColor = YELLOW_COLOR;
                for (const auto& pair : languageColorMap) {
                    if (lowerFirstLine.find(pair.first) != std::string::npos) {
                        currentCodeColor = pair.second;
                        break;
                    }
                }
                pos++;
                return true;
            } else {
                firstLineBuffer += ch;
                pos++;
                return true;
            }
        } else if (ch == '`') {
            endBackTickBuffer += '`';
            endBackTickCount++;
            pos++;
            if (endBackTickCount == 3) {
                inCodeBlock = false;
                endBackTickBuffer.clear();
                endBackTickCount = 0;
                SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);

                CodeBlock block;
                block.code = codeBuffer;
                block.language = "";
                std::string lowerFirstLine = firstLineBuffer;
                std::transform(lowerFirstLine.begin(), lowerFirstLine.end(), lowerFirstLine.begin(), ::tolower);
                for (const auto& pair : languageColorMap) {
                    if (lowerFirstLine.find(pair.first) != std::string::npos) {
                        block.language = pair.first;
                        break;
                    }
                }
                if (!block.language.empty()) {
                    std::lock_guard<std::mutex> lock(codeBlocksMutex);
                    pendingCodeBlocks.push_back(block);
                }
                codeBuffer.clear();
                firstLineBuffer.clear();
                return true;
            }
            return true;
        } else {
            codeBuffer += ch;
            SetConsoleTextAttribute(hConsole, currentCodeColor);
            std::cout << ch;
            pos++;
            return true;
        }
    }
}

void CodeBlockElement::reset() {
    inCodeBlock = false;
    codeBuffer.clear();
    startBackTickBuffer.clear();
    startBackTickCount = 0;
    endBackTickBuffer.clear();
    endBackTickCount = 0;
    collectingFirstLine = false;
    firstLineBuffer.clear();
}

bool CodeBlockElement::isActive() const {
    return inCodeBlock;
}

// MarkdownLineProcessor类实现
MarkdownLineProcessor::MarkdownLineProcessor(HANDLE hConsole) : hConsole(hConsole) {
    resetLineState();
}

void MarkdownLineProcessor::resetLineState() {
    state.headingDetected = false;
    state.headingLevel = 0;
    state.inBold = false;
    state.inItalic = false;
    state.baseColor = DEFAULT_COLOR;
    lineStart = true;
    collectingHash = false;
    hashCount = 0;
    lastMarkerChar = '\0';
    lastMarkerUsed = true;
}

void MarkdownLineProcessor::processChar(char ch) {
    if (ch == '\n') {
        state.inBold = false;
        state.inItalic = false;
        if (!state.headingDetected) {
            state.baseColor = DEFAULT_COLOR;
        }
        SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
        std::cout << ch;
        resetLineState();
        return;
    }

    if (lineStart) {
        if (hashCount == 0 && ch == '#') {
            collectingHash = true;
            hashCount = 1;
            return; 
        } else if (collectingHash) {
            if (ch == '#') {
                if (hashCount < 4) {
                    hashCount++;
                } else {
                    hashCount++;
                }
                return;
            } else {
                if (ch == ' ') {
                    state.headingDetected = true;
                    if (hashCount > 4) hashCount = 4;
                    state.headingLevel = hashCount;
                    state.baseColor = HEADING_COLORS[state.headingLevel - 1];
                    SetConsoleTextAttribute(hConsole, state.currentColor());
                    lineStart = false;
                    collectingHash = false;
                    hashCount = 0;
                    return;
                } else {
                    SetConsoleTextAttribute(hConsole, DEFAULT_COLOR);
                    for (int i = 0; i < hashCount; i++) {
                        std::cout << '#';
                    }
                    collectingHash = false;
                    hashCount = 0;
                    lineStart = false;
                }
            }
        }
        if (!collectingHash) {
            lineStart = false;
        }
    }

    if (ch == '*' || ch == '_') {
        handleFormattingMarker(ch);
        return;
    }

    std::cout << ch;
}

void MarkdownLineProcessor::handleFormattingMarker(char marker) {
    if (!lastMarkerUsed && lastMarkerChar == marker) {
        state.inBold = !state.inBold;
        lastMarkerUsed = true;
        lastMarkerChar = '\0';
    } else {
        state.inItalic = !state.inItalic;
        lastMarkerChar = marker;
        lastMarkerUsed = false;
    }
    SetConsoleTextAttribute(hConsole, state.currentColor());
}
