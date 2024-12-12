#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "rs232.h"
#include "serial.h"

#define MAX_TEXT_SIZE 8192
#define MAX_ASCII_CODES 256
#define MAX_FONTS 128
#define MAX_POINTS 100

typedef struct {
    int x;
    int y;
    int penUp; // 1 for pen up, 0 for pen down
} XY_Coordinates;

typedef struct {
    int asciiCode;
    int numPoints;
    XY_Coordinates coordinates[MAX_POINTS];
} Font_Data;

double ScaleFactor(int inputSize) {
    if (inputSize < 4) inputSize = 4; 
    if (inputSize > 10) inputSize = 10;
    return inputSize / 18.0; 
}

// Set baud rate and G-code file path
#define BAUD_RATE 115200
#define GCODE_FILE_PATH "output.gcode"
#define TEXT_FILE_PATH "test.txt"
#define ASCII_FILE_PATH "ascii_codes.txt"
#define FONT_FILE_PATH "SingleStrokeFont.txt"

// Function declarations
int ReadTextFile(const char* filePath, char* buffer, size_t bufferSize);
int SaveAsciiCodesToFile(const char* outputFilePath, const char* text);
int ReadASCIICodes(const char* filePath, int* asciiCodes, int* numCodes);
int ParseFontData(const char* filePath, Font_Data* fontArray, const int* targetASCII, int numTargets);
void GenerateGCode(const Font_Data* fontArray, const int* asciiCodes, int numCodes, const char* gcodeFilePath, double scaleFactor);
double ScaleFactor(int inputSize);
void SendCommands(char* buffer);
void SendGCodeFile(const char* filePath);

int main() {
    char textBuffer[MAX_TEXT_SIZE] = {0};
    int asciiCodes[MAX_ASCII_CODES] = {0};
    int numCodes = 0;
    Font_Data fontArray[MAX_FONTS] = {0};

    // Step 1: 读取输入文本文件
    if (!ReadTextFile(TEXT_FILE_PATH, textBuffer, sizeof(textBuffer))) {
        printf("Error: Failed to read input text file.\n");
        return 1;
    }

    if (!SaveAsciiCodesToFile(ASCII_FILE_PATH, textBuffer)) {
        printf("Error: Failed to save ASCII codes to file.\n");
        return 1;
    }

    printf("ASCII codes saved to: %s\n", ASCII_FILE_PATH);

    // Step 2: 读取 ASCII 码文件
    if (!ReadASCIICodes(ASCII_FILE_PATH, asciiCodes, &numCodes)) {
        printf("Error: Failed to read ASCII codes file.\n");
        return 1;
    }

    // Step 3: 解析字体数据并生成 G-code
    int inputSize;
    printf("Enter the desired size (4-10): ");
    if (scanf("%d", &inputSize) != 1 || inputSize < 4 || inputSize > 10) {
        printf("Error: Invalid input size. Please enter a value between 4 and 10.\n");
        return 1;
    }

    double scaleFactor = ScaleFactor(inputSize);

    if (!ParseFontData(FONT_FILE_PATH, fontArray, asciiCodes, numCodes)) {
        printf("Error: Failed to parse font data.\n");
        return 1;
    }

    GenerateGCode(fontArray, asciiCodes, numCodes, GCODE_FILE_PATH, scaleFactor);
    printf("G-code successfully generated at: %s\n", GCODE_FILE_PATH);

    // Step 4: 检查串口并发送指令
    if (CanRS232PortBeOpened() == -1) {
        printf("\nUnable to open the serial port (specified in serial.h)\n");
        exit(0);
    }

    printf("\nThe robot is ready to draw\n");

    // 发送初始化命令
    char buffer[1024];
    sprintf(buffer, "G1 X0 Y0 F1000\n");
    SendCommands(buffer);
    sprintf(buffer, "M3\n");
    SendCommands(buffer);
    sprintf(buffer, "S0\n");
    SendCommands(buffer);

    // 读取并发送 G-code 文件
    SendGCodeFile(GCODE_FILE_PATH);

    // 关闭串口
    CloseRS232Port();
    printf("Serial port closed\n");

    return 0;
}

// Send commands to the robot
// Read the G-code file line by line and send commands

// 读取文件内容到缓冲区
int ReadTextFile(const char* filePath, char* buffer, size_t bufferSize) {
    FILE* file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening file");
        return 0;
    }

    size_t currentLength = 0;
    char line[MAX_TEXT_SIZE];
    while (fgets(line, sizeof(line), file)) {
        size_t lineLength = strlen(line);
        if (currentLength + lineLength < bufferSize) {
            strcat(buffer, line);
            currentLength += lineLength;
        } else {
            printf("Warning: Content truncated due to buffer size limit.\n");
            break;
        }
    }
    fclose(file);
    return 1;
}

// 保存 ASCII 码到文件
int SaveAsciiCodesToFile(const char* outputFilePath, const char* text) {
    FILE* outputFile = fopen(outputFilePath, "w");
    if (!outputFile) {
        perror("Error opening output file");
        return 0;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        fprintf(outputFile, "%d\n", (int)text[i]);
    }
    fclose(outputFile);
    return 1;
}

// 读取 ASCII 码文件中的所有数字
int ReadASCIICodes(const char* filePath, int* asciiCodes, int* numCodes) {
    FILE* file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening ASCII codes file");
        return 0;
    }

    *numCodes = 0;
    while (fscanf(file, "%d", &asciiCodes[*numCodes]) == 1) {
        (*numCodes)++;
        if (*numCodes >= MAX_ASCII_CODES) {
            printf("Warning: Exceeded maximum number of ASCII codes.\n");
            break;
        }
    }
    fclose(file);
    return 1;
}


// 解析字体数据并提取所有目标 ASCII 码的坐标数据
int ParseFontData(const char* filePath, Font_Data* fontArray, const int* targetASCII, int numTargets) {
    FILE* file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening font data file");
        return 0;
    }

    char line[MAX_TEXT_SIZE];
    int currentASCII = -1;
    int pointCount = 0;

    while (fgets(line, sizeof(line), file)) {
        int x, y, penUp;

        if (sscanf(line, "999 %d %d", &currentASCII, &pointCount) == 2) {
            if (currentASCII < 0 || currentASCII >= MAX_FONTS) {
                currentASCII = -1;
            }
        } else if (currentASCII != -1 && sscanf(line, "%d %d %d", &x, &y, &penUp) == 3) {
            for (int i = 0; i < numTargets; i++) {
                if (currentASCII == targetASCII[i]) {
                    if (fontArray[currentASCII].numPoints < MAX_POINTS) {
                        fontArray[currentASCII].coordinates[fontArray[currentASCII].numPoints++] =
                            (XY_Coordinates){x, y, penUp};
                    }
                    break;
                }
            }
        }
    }

    fclose(file);
    return 1;
}

// Generate G-code
void GenerateGCode(const Font_Data* fontArray, const int* asciiCodes, int numCodes, const char* gcodeFilePath, double scaleFactor) {
    FILE* file = fopen(gcodeFilePath, "w");
    if (!file) {
        perror("Error opening G-code output file");
        return;
    }

    fprintf(file, "G21 ; Set units to millimeters\n");

    int xOffset = 0;
    int yOffset = 0;

    for (int i = 0; i < numCodes; i++) {
        int asciiCode = asciiCodes[i];
        const Font_Data* font = &fontArray[asciiCode];

        if (font->numPoints == 0) {
            continue;
        }

        fprintf(file, "\n; Character '%c' (ASCII %d)\n", asciiCode, asciiCode);

        for (int j = 0; j < font->numPoints; j++) {
            const XY_Coordinates* point = &font->coordinates[j];

            int scaledX = (int)(point->x * scaleFactor) + xOffset;
            int scaledY = (int)(point->y * scaleFactor) - yOffset;

            if (!point->penUp) {
                fprintf(file, "G0 X%d Y%d S0 ; Move to position (pen up)\n", scaledX, scaledY);
            } else {
                fprintf(file, "G1 X%d Y%d S1000 ; Draw to position (pen down)\n", scaledX, scaledY);
            }
        }

        if (asciiCode == '!') {
            // 换行：重置 xOffset 并增加 yOffset
            xOffset = 0;
            yOffset += (int)(15 * scaleFactor); // 根据缩放因子调整换行间距
        } else {
            // 普通字符：增加 xOffset
            xOffset += (int)(15 * scaleFactor); // 根据缩放因子调整水平间距
        }
    }

    fclose(file);
}

// Send commands to the robot
void SendCommands(char* buffer)
{
    PrintBuffer(&buffer[0]);  // Print buffer data
    WaitForReply();           // Wait for a reply from the robot
    Sleep(100);               // Ensure the robot has processed the command
}

// Read the G-code file line by line and send commands
void SendGCodeFile(const char* filePath)
{
    FILE* file = fopen(filePath, "r");
    if (!file)
    {
        perror("Error opening G-code file");
        return;
    }

    char line[256];
    char buffer[256];

    while (fgets(line, sizeof(line), file))
    {
        // Remove trailing newline or carriage return characters
        line[strcspn(line, "\r\n")] = 0;

        // Prepare to send the command
        sprintf(buffer, "%s\n", line);

        // Send the command
        SendCommands(buffer);
    }

    fclose(file);
}
