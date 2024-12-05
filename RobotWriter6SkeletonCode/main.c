#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs232.h"
#include "serial.h"

#define bdrate 115200               /* 115200 baud */
#define MAX_MOVEMENTS 1000
#define MAX_CHARACTERS 256
#define MAX_TEXT_LENGTH 1024

// Structure to hold a single movement data (X, Y, Pen)
typedef struct {
    int x;    // X-coordinate
    int y;    // Y-coordinate
    int pen;  // Pen state: 0 = pen up, 1 = pen down
} Movement;

// Structure to hold character drawing data
typedef struct {
    int num_movements;              // Number of movements for this character
    Movement movements[MAX_MOVEMENTS];  // Array of movements
} Character;

//Functions
void loadFontData(const char *filename);
void scaleFontData(float height);
void generateGCode(const char *text, float height);

//Function to send commands to the robot
void SendCommands (char *buffer )
{
    // printf ("Buffer to send: %s", buffer); // For diagnostic purposes only, normally comment out
    PrintBuffer (&buffer[0]);
    WaitForReply();
}

// Font data to store all characters
Character fontData[MAX_CHARACTERS];

// Function to load font data from file
// The font file contains movement data for characters
void loadFontData(const char *filename) {
    FILE *file = fopen(filename, "r");  // Open the font file
    if (!file) {
        printf("Error opening font file!\n");
        exit(1);
    }

    char line[256];
    int currentChar = -1;
    int numMovements = 0;

    // Read the file line by line
    while (fgets(line, sizeof(line), file)) {
        int x, y, p;
        if (strncmp(line, "999", 3) == 0) {  // Start of a new character definition
            if (currentChar != -1) {
                fontData[currentChar].num_movements = numMovements;  // Save the movements count
            }
            // Parse the character ID and number of movements
            sscanf(line, "999 %d %d", &currentChar, &numMovements);
        } else {  // Parse movement data (X, Y, Pen state)
            sscanf(line, "%d %d %d", &x, &y, &p);
            fontData[currentChar].movements[numMovements++] = (Movement){x, y, p};
        }
    }

    // Finalize the last character's movements
    if (currentChar != -1) {
        fontData[currentChar].num_movements = numMovements;
    }

    fclose(file);  // Close the file
}

// Function to scale the font data based on the desired height
void scaleFontData(float height) {
    float scaleFactor = height / 18.0f;  // Calculate the scale factor

    // Scale the movements for each character
    for (int i = 0; i < MAX_CHARACTERS; i++) {
        if (fontData[i].num_movements > 0) {  // Only process characters with movements
            for (int j = 0; j < fontData[i].num_movements; j++) {
                // Scale the X and Y coordinates and cast to integer
                fontData[i].movements[j].x = (int)((float)fontData[i].movements[j].x * scaleFactor);
                fontData[i].movements[j].y = (int)((float)fontData[i].movements[j].y * scaleFactor);
            }
        }
    }
}

// Function to generate G-code for drawing the input text
void generateGCode(const char *text, float height) {
    scaleFontData(height);  // Scale the font data based on the height

    // Initialize drawing parameters
    int x_pos = 0;                      // Current X position
    int y_pos = -(int)height;           // Start below the X-axis by the input height
    int penState = 0;                   // Current pen state: 0 = pen up, 1 = pen down
    int charWidth = (int)(height * 1.0f);  // Width of each character, scaled with height
    int wordGap = charWidth;            // Gap between words
    int lineGap = (int)(height + 5.0f); // Vertical gap between lines
    int maxLineWidth = 100;             // Maximum line width in mm
    int lowestY = y_pos;                // Tracks the lowest Y point on the current line
    int minY = -90 - (int)height;       // Y-axis limit adjusted for starting height

    char buffer[256];
    char word[128];  // Buffer to hold a single word
    int wordIndex = 0;

    // Process the input text character by character
    for (const char *ptr = text; *ptr != '\0'; ptr++) {
        char c = *ptr;

        // Handle word delimiters (space, newline, end of text)
        if (c == ' ' || c == '\n' || *(ptr + 1) == '\0') {
            // Add the last character to the word if it's not already processed
            if (*(ptr + 1) == '\0' && c != ' ' && c != '\n') {
                word[wordIndex++] = c;
            }

            word[wordIndex] = '\0';  // Null-terminate the word

            // Calculate the word width
            int wordWidth = (int)((size_t)strlen(word) * (size_t)charWidth);

            // Move to the next line if the word doesn't fit
            if (x_pos + wordWidth > maxLineWidth) {
                y_pos = lowestY - lineGap;
                if (y_pos < minY) {  // Check for Y-axis limit
                    printf("Error: Text exceeds Y-axis limit.\n");
                    return;
                }
                x_pos = 0;           // Reset X position
                lowestY = y_pos;     // Reset lowest Y for the new line
                sprintf(buffer, "G0 X0 Y%d\n", y_pos); 
                SendCommands(buffer); // Move to the new line
            }

            // Process each character in the word
            for (int i = 0; word[i] != '\0'; i++) {
                unsigned char currentChar = (unsigned char)word[i];  // Ensure unsigned subscript

                if (currentChar >= 32 && currentChar <= 126) {  // Printable ASCII characters only
                    Character charData = fontData[currentChar];
                    for (int j = 0; j < charData.num_movements; j++) {
                        Movement m = charData.movements[j];

                        // Calculate the new X and Y positions
                        int newX = m.x + x_pos;
                        int newY = m.y + y_pos;

                        if (newY < lowestY) {  // Update the lowest Y position
                            lowestY = newY;
                        }

                        // Handle pen state changes
                        if (m.pen != penState) {
                            penState = m.pen;
                            sprintf(buffer, penState == 1 ? "S1000\n" : "S0\n");  // Pen up or down
                            SendCommands(buffer);
                        }

                        // Output movement G-code
                        sprintf(buffer, penState == 1 ? "G1 X%d Y%d\n" : "G0 X%d Y%d\n", newX, newY);
                        SendCommands(buffer);
                    }
                    x_pos += charWidth;  // Move the X position for the next character
                }
            }
            x_pos += wordGap;  // Add gap after a word

            // Handle newline characters
            if (c == '\n') {
                y_pos = lowestY - lineGap;
                if (y_pos < minY) {  // Check for Y-axis limit
                    printf("Error: Text exceeds Y-axis limit.\n");
                    return;
                }
                x_pos = 0;          // Reset X position
                lowestY = y_pos;    // Reset lowest Y for the new line
                sprintf(buffer, "G0 X0 Y%d\n", y_pos);
                SendCommands(buffer);  // Move to the new line
            }

            wordIndex = 0;  // Reset the word buffer
        } else {
            // Accumulate characters for the current word
            word[wordIndex++] = c;
        }
    }

    // Finalize G-code with pen up and return to (0, 0)
    if (penState != 0) {
        sprintf(buffer, "S0\n");  // Pen up
        SendCommands(buffer);
    }
    sprintf(buffer, "G0 X0 Y0\n");  // Return to origin
    SendCommands(buffer);
}


int main()
{

    char buffer[256];

    // If we cannot open the port then give up immediately
    if ( CanRS232PortBeOpened() == -1 )
    {
        printf ("\nUnable to open the COM port (specified in serial.h) ");
        exit (0);
    }

    // Time to wake up the robot
    printf ("\nAbout to wake up the robot\n");

    // We do this by sending a new-line
    sprintf (buffer, "\n");
     // printf ("Buffer to send: %s", buffer); // For diagnostic purposes only, normally comment out
    PrintBuffer (&buffer[0]);
    Sleep(100);

    // This is a special case - we wait  until we see a dollar ($)
    WaitForDollar();

    printf ("\nThe robot is now ready to draw\n");

        //These commands get the robot into 'ready to draw mode' and need to be sent before any writing commands
    sprintf (buffer, "G1 X0 Y0 F1000\n");
    SendCommands(buffer);
    sprintf (buffer, "M3\n");
    SendCommands(buffer);
    sprintf (buffer, "S0\n");
    SendCommands(buffer);

    char fontFile[] = "SingleStrokeFont.txt";
    loadFontData(fontFile);

    int height;
    printf("Enter a text height between 4 and 10mm:");
    scanf("%d",height);

    if (height < 4 || height > 10) {
        printf("Error: Height must be between 4 and 10mm.\n");
        return 1;
    }

    char textFileName[256];
    printf("Enter the name of the text file: ");
    scanf("%s", textFileName);

    FILE *textFile = fopen(textFileName, "r");
    if (!textFile) {
        printf("Error opening text file!\n");
        return 1;
    }

   char text[MAX_TEXT_LENGTH] = {0};
    size_t index = 0;

    while (fgets(&text[index], sizeof(text) - index, textFile)) {
        index += strlen(&text[index]);
        if (index >= sizeof(text) - 1) {
            break;
        }
    }

    fclose(textFile);
    generateGCode(text, (float)height);

    // Before we exit the program we need to close the COM port
    CloseRS232Port();
    printf("Com port now closed\n");

    return (0);
}

// Send the data to the robot - note in 'PC' mode you need to hit space twice
// as the dummy 'WaitForReply' has a getch() within the function.

