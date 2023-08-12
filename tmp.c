#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int main() {
    char buffer[8] = {0}; // Initialize an 8-byte buffer with zeros
    short value = 12345;  // Example value

    // Convert the short to network order and store it directly in the buffer
    *(buffer + 2) = value & 0xFF;

    printf("%hhd\n", value & 0xFF);
    printf("%hhd\n", (value >> 8) & 0xFF);
    // Print the buffer for verification
    for (int i = 0; i < 8; i++) {
        printf("%02x ", (unsigned char)buffer[i]);
    }
    printf("\n");
    printf("%04x\n", htons(value));
    printf("%04x\n", value);
    return 0;
}
