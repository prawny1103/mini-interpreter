// cc -std=c11 -Wall -Werror -o runml main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {false, true} bool;

#define LINE_LENGTH 1024
#define MAX_IDENTIFIERS 50

FILE *code;
FILE *compiled;

char variableNames[MAX_IDENTIFIERS][12];
char variableValues[MAX_IDENTIFIERS][50];

char mainFunctionLines[LINE_LENGTH * 4];
char functionBodies[50][LINE_LENGTH * 4];

typedef struct {
    char identifier[12];
    char **parameters;
    char *functionBody;
} Function;

int isComment(char character) {
    if (character == '#') {
        return true;
    } else {
        return false;
    }
}

int findVariable(char name[]) {
    for (int i = 0; i < 50; i++) {
        if (strcmp(variableNames[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void variableAssignment(char name[], char *value) {
    int index = findVariable(name);
    if (index == -1) {
        for (int i = 0; i < 50; i++) {
            if (variableNames[i][0] == '\0') {
                strcpy(variableNames[i], name);
                strcpy(variableValues[i], value);
                return;
            }
        }
    } else {
        strcpy(variableValues[index], value);
        return;
    }
}

int isNumeric(const char *str) {
    int decimalPointCount = 0;

    // Check for empty string
    if (*str == '\0') {
        return false; // Not numeric
    }

    // Check each character
    while (*str) {
        if (*str == '.') {
            // Allow only one decimal point
            if (decimalPointCount == 0) {
                decimalPointCount++;
            } else {
                return false; // More than one decimal point
            }
        } else if (*str < '0' || *str > '9') {
            return false; // Not numeric
        }
        str++;
    }
    return true; // Numeric
}


Function functions[50];
int functionCount = 0;

char currentFunction[1024];

void processFunctions(char *line, Function *funcs, int *currentFunc, int *paramCount, size_t *bodySize) {
    static int isBody = 0;

    if (!isBody) {
        // Read the first line for identifier and parameters
        char *token = strtok(line, " ");
        if (token && strcmp(token, "function") == 0) {
            token = strtok(NULL, " ");
            if (token) {
                strncpy(funcs[*currentFunc].identifier, token, sizeof(funcs[*currentFunc].identifier) - 1);
                funcs[*currentFunc].identifier[sizeof(funcs[*currentFunc].identifier) - 1] = '\0';

                // Read parameters
                while ((token = strtok(NULL, " \n"))) {
                    funcs[*currentFunc].parameters = realloc(funcs[*currentFunc].parameters, sizeof(char *) * (*paramCount + 1));
                    funcs[*currentFunc].parameters[*paramCount] = strdup(token);
                    (*paramCount)++;
                }
                funcs[*currentFunc].parameters = realloc(funcs[*currentFunc].parameters, sizeof(char *) * (*paramCount + 1));
                funcs[*currentFunc].parameters[*paramCount] = NULL; // Null-terminate the parameters array
            }
            isBody = 1;
        }
    } else {
        // Read the function body
        if (line[0] == '\t' || line[0] == ' ') {
            size_t len = strlen(line);
            funcs[*currentFunc].functionBody = realloc(funcs[*currentFunc].functionBody, *bodySize + len + 1);
            strcpy(funcs[*currentFunc].functionBody + *bodySize, line);
            *bodySize += len;
        } else {
            // New function starts, reset for the next function
            (*currentFunc)++;
            *paramCount = 0;
            *bodySize = 0;
            isBody = 0;
            processFunctions(line, funcs, currentFunc, paramCount, bodySize);
        }
    }
}

void freeFunctions(Function *funcs, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = 0; funcs[i].parameters[j]; j++) {
            free(funcs[i].parameters[j]);
        }
        free(funcs[i].parameters);
        free(funcs[i].functionBody);
    }
}

char* removeWhitespace(const char *str) {
    int length = strlen(str);
    char *result = (char *)malloc(length + 1); // Allocate memory for the result
    int i = 0, j = 0;

    while (str[i]) {
        // Check for whitespace characters (space, tab, newline)
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
            result[j++] = str[i];
        }
        i++;
    }
    result[j] = '\0'; // Null-terminate the string

    return result;
}

char* interpretLine(char *line) {
    int index = 0, i = 0;
    while (line[index] == ' ' || line[index] == '\t' || line[index] == '\n') {
        index++;
    }
    while (line[index] != '\0') {
        line[i++] = line[index++];
    }
    line[i] = '\0';

    static char buffer[LINE_LENGTH];
    char identifier[12];
    char value[50];
    char operand1[50];
    char operand2[50];
    char operator;

    // Check for variable assignment
    if (sscanf(line, "%s <- %s", identifier, value) == 2) {
        if (isNumeric(value)) {
            variableAssignment(identifier, value);
            return "\n";
        }
        else if (findVariable(identifier) == -1) {
            snprintf(buffer, sizeof(buffer), "double %s = %s;\n", identifier, value);
            return buffer;
        } else {
            snprintf(buffer, sizeof(buffer), "%s = %s;\n", identifier, value);
            return buffer;
        }
    }

    // Check for print statement
    if (strncmp(line, "print", 5) == 0) {
        char *restOfStatement = line + 5;
        char *arguments = removeWhitespace(restOfStatement);

        strcpy(buffer, "printf(");
        strcat(buffer, "\"%f\", ");
        strcat(buffer, arguments);
        strcat(buffer, ");\n");

        return buffer;
    }

    // Check for arithmetic operation
    if (sscanf(line, "%s %c %s", operand1, &operator, operand2) == 3) {
        if (strchr("+-*/", operator)) {
            snprintf(buffer, sizeof(buffer), "%s %c= %s;\n", operand1, operator, operand2);
            return buffer;
        }
    }

    if (strncmp(line, "return", 6) == 0) {
        snprintf(buffer, sizeof(buffer), "%s;\n", line);

        return buffer;
    }

    // If none of the above, return the line as is
    snprintf(buffer, sizeof(buffer), "%s", line);
    return buffer;
}

void writeFile(int noOfFunctions) {
    pid_t pid = getpid();
    char filename[256];
    snprintf(filename, sizeof(filename), "%lld.c", pid);
    
    printf("Code saved to %s", filename);
    compiled = fopen(filename, "a");

    fprintf(compiled, "#include <stdio.h>\n\n");

    for (int i = 0; i < sizeof(variableNames) / sizeof(variableNames[0]); i++) {
        if (variableNames[i][0] != '\0') {
            fprintf(compiled, "double %s = %s;\n", variableNames[i], variableValues[i]);
        }
    }

    for (int i = 0; i <= noOfFunctions; i++) {
        char buffer[LINE_LENGTH];
        int offset = snprintf(buffer, sizeof(buffer), "double %s(", functions[i].identifier);

        for (int j = 0; functions[i].parameters[j]; j++) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "double %s", functions[i].parameters[j]);
            
            if (functions[i].parameters[j + 1]) {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
            }
        }
        snprintf(buffer + offset, sizeof(buffer) - offset, ") { \n");

        fprintf(compiled, buffer);
        // fprintf(compiled, interpretLine(functions[i].functionBody));
        char *bodyLine = strtok(functions[i].functionBody, "\n");
        while (bodyLine) {
            fprintf(compiled, "%s\n", interpretLine(bodyLine));
            bodyLine = strtok(NULL, "\n");
        }
        fprintf(compiled, "}\n");
    }

    // Write functions

    fprintf(compiled, "\nint main() {\n");
    fprintf(compiled, "%s\n", mainFunctionLines);
    fprintf(compiled, "return 0;\n");
    fprintf(compiled, "}\n");
}

int main(int argcount, char *argvalue[]) {
    if (argcount != 2) {
        printf("error");
        exit(1);
    } else {
        for (int i = 0; i < MAX_IDENTIFIERS; i++) {
            functions[i].parameters = NULL;
            functions[i].functionBody = NULL;
        }
        code = fopen(argvalue[1], "r");
        char line[LINE_LENGTH];

        int paramCount = 0;
        int functionCount = 0;
        size_t bodySize = 0;

        while (fgets(line, sizeof(line), code)!= NULL) {
            if (isComment(line[0])) {
                continue;
            } else {
                if (strncmp(line, "function", 8) == 0 || line[0] == '\t' || line[0] == ' ') {
                    processFunctions(line, functions, &functionCount, &paramCount, &bodySize);
                } else {
                    for (int i = 0; i < sizeof(mainFunctionLines) / sizeof(mainFunctionLines[0]); i++) {
                        strcat(mainFunctionLines, interpretLine(line));
                        break;
                    }
                }
            }
        }

        writeFile(functionCount);
        fclose(code);
    }

    freeFunctions(functions, functionCount);
    return 0;
}
