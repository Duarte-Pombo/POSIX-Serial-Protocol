
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

// formating definitions
#define BOLD "\033[1m"
#define ITALIC "\033[3m"
#define RESET "\033[0m"
#define RED "\033[1;31m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define BLUE "\033[1;34m"

// packaging bytes
#define START_CONTROL 0x01
#define DATA_CONTROL 0x02
#define END_CONTROL 0x03
#define T_FILE_SIZE 0x00
#define T_FILE_NAME 0x01

/*
typedef enum
{
    LlTx,
    LlRx,
} LinkLayerRole;

typedef struct
{
    char serialPort[50];
    LinkLayerRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} LinkLayer
*/

LinkLayer llConnection;
char fileName[256] = {0};
unsigned long int fileSize = 0;

int createControlPacket (const char* fileName, const unsigned long fileSize, bool start);
int createInfoPacket (const unsigned char * buffer, const unsigned int size );
int createEndPacket (void);
void printEfficiencyMetrics(unsigned long int  fileSize, double transferTime, int baudRate);

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // ----- SET UP -----

    struct timespec startClk , endClk;
    clock_gettime(CLOCK_MONOTONIC, &startClk);

    if (strcmp (role, "tx") == 0){  // transmit
        strncpy(llConnection.serialPort, serialPort, sizeof(llConnection.serialPort)-1);
        llConnection.serialPort[sizeof(llConnection.serialPort)-1] = '\0';
        llConnection.role = LlTx;
    }
    else if (strcmp (role, "rx") == 0){  // receive
        strncpy(llConnection.serialPort, serialPort, sizeof(llConnection.serialPort)-1);
        llConnection.serialPort[sizeof(llConnection.serialPort)-1] = '\0';
        llConnection.role = LlRx;
    }
    else {
        perror("incorrect role");
        return;
    }

    llConnection.baudRate = baudRate;
    llConnection.nRetransmissions = nTries;
    llConnection.timeout = timeout;

    // ----- LLOPEN() -----
    if (llopen (llConnection) == 0) {
        printf(GREEN "Connection successfully established \n" RESET);
    } else {
        printf(RED "Connection establishment error, terminating \n" RESET);
        return;
    }

    // ----- LLWRITE() -----
    FILE *fptr;
    if (llConnection.role==LlTx) {
        
        fptr = fopen(filename, "rb");

        if (fptr == NULL){
            perror(RED "Failed to open designated file \n" RESET);
            return;
        }

        fseek(fptr, 0, SEEK_END);
        fileSize = ftell(fptr);
        rewind(fptr);


        // Send START PACKET
        if (createControlPacket(filename, fileSize, true)) {
            printf (GREEN "Start packet sent and received by the ll \n" RESET);
        } else {
            printf (RED "failed to send start packet to ll\n" RESET);
            return;
        }

        // while file not EOF send DATA INFO packet
        unsigned char buffer[MAX_PAYLOAD_SIZE];
        unsigned int bytes_read;

        while ((bytes_read = fread(buffer, 1, MAX_PAYLOAD_SIZE, fptr)) > 0){
            if (createInfoPacket(buffer, bytes_read)){
                printf(GREEN "Data packet sent and received by the ll \n" RESET);
            }
            else{
                printf(RED "error creating data packet \n" RESET);
                return;
            }
        }

        // send END control packet
        if (createControlPacket(filename, fileSize, false)) printf (GREEN "successfully sent end packet \n" RESET);
        else  printf (RED "error sending end packet \n" RESET);

    } else if (llConnection.role == LlRx) {
        unsigned char packet[MAX_PAYLOAD_SIZE + 3]; // data payload + header
        int packetSize;


        bool STOP = false;
        
        // get DATA packets until END packet
        while (!STOP) {
            packetSize = llread(packet);
            if (packetSize == -1) {
                printf("llread failed, no action\n");
                continue;
            }
            if (packetSize == 0) continue; // no bytes read
            
            switch (packet[0]) {
                case START_CONTROL: {
                    
                    printf (GREEN "Received START PACKET \n" RESET);
                    // get info of file from start packet TLV(s)
        
                    int i = 1; //skip control byte
    
        
                    while (i < packetSize){
                        unsigned char T = packet[i++];
                        unsigned char L = packet[i++];
        
                        if (L+i > packetSize) {
                            perror (RED "start packet wrongly formated\n" RESET);
                            return;
                        }
        
                        if (T == T_FILE_NAME) {
                            memcpy(fileName, &packet[i], L);
                            fileName[L] = '\0';
                            printf(BLUE "File Name: %s\n" RESET, fileName);
                        } else if (T == T_FILE_SIZE) {
                            for (unsigned int j = 0; j < L; j++) 
                                fileSize |= packet[i + j] << (8 * j);
                            printf(BLUE "File size: %li Bytes\n" RESET, fileSize);
                        } else
                            printf(YELLOW "Not configured or unknown TLV type in START package \n" RESET);
        
                        i += L;
                    }

                    // Create a new file for writing with received- before extension
                    char newFileName[270];
                    char *dot = strrchr(fileName, '.');
                    if (dot != NULL) {
                        // Found an extension - insert received- before it
                        size_t nameLen = dot - fileName;
                        strncpy(newFileName, fileName, nameLen);
                        newFileName[nameLen] = '\0';
                        strcat(newFileName, "-received");
                        strcat(newFileName, dot);
                    } else {
                        // No extension found - append _received
                        strcpy(newFileName, fileName);
                        strcat(newFileName, "-received");
                    }
                    
                    fptr = fopen(newFileName, "wb");
                    if (fptr == NULL) {
                        printf(RED "Failed to create file: %s\n" RESET, newFileName);
                        return;
                    }
                    printf(GREEN "Created new file: %s\n" RESET, newFileName);
        
                    break;
                }



                case DATA_CONTROL: {
                    if (fptr == NULL) {
                        printf(RED "Received DATA packet before valid START packet\n" RESET);
                        return;
                    }

                    unsigned char len2 = packet[1];
                    unsigned char len1 = packet[2];

                    unsigned int dataSize = (len2 << 8) | len1;

                    // Write data to file and verify bytes written
                    size_t bytesWritten = fwrite(&packet[3], 1, dataSize, fptr);
                    if (bytesWritten != dataSize) {
                        printf(RED "Error writing to file: expected %d bytes, wrote %zu bytes\n" RESET, dataSize, bytesWritten);
                        fclose(fptr);
                        return;
                    }
                    printf(GREEN "Wrote packet to file: %zu bytes\n" RESET, bytesWritten);
                    break;
                }

                case END_CONTROL: {
                    printf(BLUE "Received end packet \n" RESET);
                    STOP = TRUE;
                    break;
                }

                default: printf(RED "error: got unknown packet type \n");
            }
        }
    }



    fclose(fptr);
    llclose();
    printf(GREEN "transfer complete \n" RESET);

    clock_gettime(CLOCK_MONOTONIC, &endClk);

    double runtime = (double) (endClk.tv_sec - startClk.tv_sec)
                     + (double) (endClk.tv_nsec - startClk.tv_nsec) / 1e9;

    printEfficiencyMetrics(fileSize, runtime, llConnection.baudRate);
    return;
}




int createControlPacket (const char* fileName, const unsigned long fileSize, bool start) {
//    unsigned char startPacket [packetSize]; = {
//            START_CONTROL, // 1 BYTE
//            T_FILE_SIZE, // 1 BYTE
//            sizeof (fileSize), // 1 BYTE
//            (char) fileSize, // sizeof(fileSize) BYTES
//            T_FILE_NAME, // 1 BYTE
//            sizeof  (fileName), // 1 BYTE
//            fileName // (int) strlen (fileName) BYTES
//    };

    int packetSize = 1 + 1 + 1 + sizeof(fileSize) + 1 + 1 + (int)strlen(fileName);

    unsigned char startPacket [packetSize];
    int i = 0;

    startPacket[i++] = start? START_CONTROL : END_CONTROL;
    startPacket[i++] = T_FILE_SIZE;
    startPacket[i++] = sizeof(fileSize);
    memcpy(&startPacket[i], &fileSize, sizeof(fileSize));
    i += sizeof(fileSize);
    startPacket[i++] = T_FILE_NAME;
    startPacket[i++] = (int)strlen(fileName);
    memcpy(&startPacket[i], fileName,  (int)strlen(fileName));

    if (llwrite(startPacket, packetSize) > 0){
        return 1;
    }
    return 0;
}




int createInfoPacket (const unsigned char * buffer, const unsigned int size){
    printf(YELLOW"creating packet INFO packet of %d bytes of info\n" RESET, size);

//    unsigned char infoBuffer [packetSize]; = {
//            DATA_CONTROL, // 1 BYTE
//            NUMBER_OF_PACKETS, // 2 BYTES
//            DATA // 1 to MAXPAYLOAD BYTES
//    };
    unsigned char L1 = size;
    unsigned char L2 = (size >> 8);
    unsigned char infoBuffer [1+2+size];

    int i = 0;
    infoBuffer[i++] = DATA_CONTROL;
    infoBuffer[i++] = L2;
    infoBuffer[i++] = L1;

    memcpy(&infoBuffer[i], buffer, size);
    i += size;

    if (llwrite(infoBuffer, i) > 0){
        return 1;
    }
    return 0;
}

void printEfficiencyMetrics(unsigned long int  fileSize, double runtime, int baudRate) {
    double throughput = (fileSize * 8.0) / runtime; //bps
    double efficiency = throughput / baudRate;

    printf("file transfer duration: %.6f s\n", runtime);
    printf("file Size: %ld bytes\n", fileSize);
    printf("throughput (R): %.2f bps\n", throughput);
    printf("efficiency (S): %.4f\n", efficiency);
}