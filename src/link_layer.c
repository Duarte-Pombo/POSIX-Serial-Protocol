// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

//misc
#define _POSIX_SOURCE 1 // POSIX compliant source

//std definitions
#define FALSE 0
#define TRUE 1

// formating definitions
#define BOLD "\033[1m"
#define ITALIC "\033[3m"
#define RESET "\033[0m"
#define RED "\033[1;31m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define BLUE "\033[1;34m"

// define message config
#define BAUDRATE 38400
#define BUF_SIZE 256
#define FLAG             0x7E
#define ESC              0x7D
#define XOR_VALUE        0x20
#define ADDRESS_SENDER   0x03
#define ADDRESS_RECEIVER 0x02
#define SET              0x03
#define UA               0x07
#define RR0 0xAA
#define RR1 0xAB
#define REJ0 0x54
#define REJ1 0x55
#define DISC 0x0B
#define START_CONTROL 0x01
#define DATA_CONTROL 0x02
#define END_CONTROL 0x03
#define T_FILE_SIZE 0x00
#define T_FILE_NAME 0x01

// global vars
bool STOP = false;
int attempts = 0;
int nRetransmissions = 0;
int timeout = 0;
LinkLayerRole role;
unsigned char Ns = 0;
bool alarmEnabled = FALSE;
LinkLayerState state = START;

//func declarations
int buildInformationFrame(const unsigned char *payload, int payloadSize, unsigned char *frame);
int buildSupervisionFrame(LinkLayerRole role, unsigned char control, unsigned char *frame);
void alarmHandler(int signal);

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    printf("\n");
    printf("SETTINGS SENT TO THE LL BY THE API");
    printf("\n");
    printf("Serial Port: %s\n", connectionParameters.serialPort);
    printf("Role: %s\n", connectionParameters.role == LlTx ? "Transmitter (LlTx)" : "Receiver (LlRx)");
    printf("Baud Rate: %d\n", connectionParameters.baudRate);
    printf("Number of Retransmissions: %d\n", connectionParameters.nRetransmissions);
    printf("Timeout: %d\n", connectionParameters.timeout);

    nRetransmissions = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;
    role = connectionParameters.role;



    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0){
        perror("openSerialPort");
        exit(-1);
    }

    printf("Serial port %s opened\n", connectionParameters.serialPort);
    
    
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1){
        perror("sigaction");
        exit(1);
    }
    
    state = START;
    unsigned char bcc;
    STOP = FALSE;
    attempts = 0;
    unsigned char readByte;
    
    if (role == LlTx) {

        unsigned char writeBuf[5];
        buildSupervisionFrame(role, SET, writeBuf);

        while (attempts < nRetransmissions && !STOP) {
            printf(YELLOW "Sending SET frame (attempt %d)...\n" RESET, attempts + 1);
            int w = writeBytesSerialPort(writeBuf, 5);
            printf("%d bytes written to serial port\n", w);

            // start alarm
            alarmEnabled = TRUE;
            alarm(timeout);


            while (alarmEnabled && !STOP) {
                int bytesRead = readByteSerialPort(&readByte);
                if (bytesRead <= 0) continue;

                printf("Received byte >   " YELLOW "0x%02X \n" RESET, readByte);

                switch (state) {
                    case START:
                        if (readByte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (readByte == ADDRESS_RECEIVER) { state = A_RCV; bcc = readByte; }
                        else if (readByte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (readByte == UA) { state = C_RCV; bcc ^= readByte; }
                        else if (readByte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (readByte == bcc) { state = BCC1_OK; }
                        else if (readByte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC1_OK:
                        if (readByte == FLAG) {
                            printf(GREEN "UA frame received\n" RESET);
                            STOP = TRUE;
                            alarmEnabled = FALSE;
                            alarm(0);
                        } else {
                            state = START;
                        }
                        break;
                    default:
                        state = START;
                        break;
                }
            }

            if (!STOP) {
                printf(RED "Timeout or no UA received; will retry\n" RESET);
            }
        }

        if (!STOP) {
            fprintf(stderr, RED "Failed to receive UA after %d attempts" RESET "\n", attempts);
            if (closeSerialPort() < 0) perror("closeSerialPort");
            return -1;
        }

        printf(GREEN "Link opened as Transmitter\n" RESET);

    } else {

        while (!STOP) {
            int bytesRead = readByteSerialPort(&readByte);
            if (bytesRead <= 0) continue;

            switch (state) {
                case START:
                    if (readByte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (readByte == ADDRESS_SENDER) { state = A_RCV; bcc = readByte; }
                    else if (readByte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (readByte == SET) { state = C_RCV; bcc ^= readByte; }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (readByte == (SET ^ ADDRESS_SENDER)) { state = BCC1_OK; }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC1_OK:
                    if (readByte == FLAG) {
                        printf(GREEN "SET frame received\n" RESET);
                        STOP = TRUE;
                    } else {
                        state = START;
                    }
                    break;
                default:
                    state = START;
                    break;
            }
        }

        unsigned char buf[5];
        buildSupervisionFrame(role, UA, buf);
        printf(YELLOW "Sending UA confirmation frame...\n" RESET);
        writeBytesSerialPort(buf, 5);
        printf(GREEN "Link opened as Receiver\n" RESET);
    }

    return 0;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // Maximum frame size: F + A + C + BCC1 + (2 * payload for stuffing) + BCC2 + F
    unsigned char infoFrame[4 + 2 * bufSize + 2];
    
    // Build the I-frame using our helper
    int frameSize = buildInformationFrame(buf, bufSize, infoFrame);

    // send frame and wait for RR/REJ using state machine with retransmissions
    int w = writeBytesSerialPort(infoFrame, frameSize);
    if (w <= 0) {
        perror("writeBytesSerialPort");
        return -1;
    }

    printf(GREEN "Sent info frame (size=%d) with seq=%d\n" RESET, frameSize, Ns);

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1){
        perror("sigaction");
        exit(1);
    }

    attempts = 0;
    state = START;
    STOP = FALSE;
    unsigned char bcc;
    unsigned char readByte;
    unsigned char expectedRR = (Ns + 1) % 2 == 0 ? RR0 : RR1;
    
    
    while (attempts < nRetransmissions && !STOP) {
        
        alarmEnabled = TRUE;
        alarm(timeout);

        while (alarmEnabled && !STOP) {
            int bytesRead = readByteSerialPort(&readByte);
            if (bytesRead <= 0) continue;

            switch (state) {
                case START:
                    if (readByte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (readByte == ADDRESS_RECEIVER) { state = A_RCV; bcc = readByte; }
                    else if (readByte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (readByte == expectedRR) { state = C_RCV; bcc ^= readByte; }
                    else if (readByte == REJ0 || readByte == REJ1) {
                        printf(YELLOW "llwrite: received REJ -> retransmit\n" RESET);
                        state = START;
                        alarmEnabled = FALSE; // trigger retransmit
                    }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (readByte == bcc)  state = BCC1_OK;
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC1_OK:
                    if (readByte == FLAG) {
                        alarmEnabled = FALSE;
                        alarm(0);
                        STOP = TRUE;
                        Ns = (Ns + 1) % 2;
                        printf(GREEN "llwrite: valid RR received\n" RESET);
                    } else state = START;
                    break;
                default:
                    state = START;
                    break;
            }
        }

        if (!STOP) {
            // retransmit
            printf(RED "llwrite: timeout or invalid response, retransmitting (try %d)\n" RESET, attempts);
            printf(GREEN "Sent info frame (size=%d) with seq=%d\n" RESET, frameSize, Ns);
            writeBytesSerialPort(infoFrame, frameSize);
        }
    }

    if (!STOP) {
        printf(RED "llwrite: failed after %d attempts\n" RESET, nRetransmissions);
        return -1;
    }

    return frameSize; // success: return number of bytes written
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) // return number of payload bytes read or -1
{

    state = START;
    STOP = FALSE;
    unsigned char control = 0;
    unsigned char bcc1 = 0;
    unsigned char dataBuf[MAX_PAYLOAD_SIZE + 16];
    unsigned char responseBuf[5];
    int dataIndex = 0;
    
    unsigned char byte;
    
    while (!STOP) {
        int r = readByteSerialPort(&byte);
        if (r <= 0) continue;

        switch (state) {
            case START:
                if (byte == FLAG) state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (byte == FLAG) {
                    /* stay */
                } else if (byte == ADDRESS_SENDER) {
                    bcc1 = byte;
                    state = A_RCV;
                } else {
                    state = START;
                }
                break;
                
            case A_RCV:
                if (byte == 0x00 || byte == 0x80) {
                    bcc1 ^= byte;
                    control = byte == 0x80? 1 : 0;
                    state = C_RCV;
                } else {
                    state = START;
                }
                break;

            case C_RCV:
                if (byte == bcc1) {
                    /* header ok, check Ns */
                    if (control == Ns) {  // Ns here serves as expected Ns
                        state = READING_DATA;
                    } else { //ignore
                        printf("Not expected Ns\n");
                        state = START;
                    }
                } else if (byte == FLAG) {
                    state = FLAG_RCV;
                } else {
                    printf("Error with header");
                    state = START;
                }
                break;


            case READING_DATA:
                // start destuffing
                if (byte == ESC) { // Found escape character - next byte needs destuffing
                    state = FOUND_ESC;
                    break;

                } else if (byte == FLAG) { // End of frame
                    unsigned char bcc2 = dataBuf[dataIndex-1];
                    dataBuf[dataIndex - 1] = '\0';
                    
                    //check bcc2
                    unsigned char check = dataBuf[0];
                    for (int i = 1; i < dataIndex; i++) 
                        check ^= dataBuf[i];

                    if (check == bcc2) {
                        // Reply by sending RR(Ns + 1)
                        unsigned char rr = Ns == 0? RR1 : RR0;
                        buildSupervisionFrame(role, rr, responseBuf);
                        writeBytesSerialPort(responseBuf, 5);
                        
                        //return data packet
                        memcpy(packet, dataBuf, dataIndex); 

                        Ns = (Ns + 1) % 2;

                    } else {
                        printf("llread: BCC2 error\n");
                        unsigned char rej = Ns == 0? REJ0 : REJ1;
                        buildSupervisionFrame(role, rej, responseBuf);
                        writeBytesSerialPort(responseBuf, 5);
                        return -1;
                    }
                    
                    STOP = TRUE;
                    
                } else {
                    // Normal byte - store it
                        dataBuf[dataIndex++] = byte;
                }
                break;


            case FOUND_ESC:
                byte ^= XOR_VALUE;
                dataBuf[dataIndex++] = byte;
                state = READING_DATA;
                break;


            default:
                state = START;
                break;
        }
    }

    return dataIndex - 1;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose() {
    printf(BLUE "Closing connection...\n" RESET);

    unsigned char frame[5];
    unsigned char readByte;
    LinkLayerState state = START;
    unsigned char bcc;
    STOP = FALSE;

    extern LinkLayer llConnection;
    LinkLayerRole role = llConnection.role;

    if (role == LlTx) {
        printf(YELLOW "Transmitter initiating DISC...\n" RESET);

        // Send DISC
        buildSupervisionFrame(role, DISC, frame);
        writeBytesSerialPort(frame, 5);

        // Wait for DISC from receiver using state machine
        state = START;
        while (!STOP) {
            int bytesRead = readByteSerialPort(&readByte);
            if (bytesRead <= 0) continue;

            switch (state) {
                case START:
                    if (readByte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (readByte == ADDRESS_RECEIVER) { state = A_RCV; bcc = readByte; }
                    else if (readByte != FLAG) state = START;
                    break;

                case A_RCV:
                    if (readByte == DISC) { state = C_RCV; bcc ^= readByte; }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case C_RCV:
                    if (readByte == bcc) state = BCC1_OK;
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case BCC1_OK:
                    if (readByte == FLAG) {
                        printf(GREEN "Received DISC from receiver\n" RESET);
                        STOP = TRUE;
                    } else state = START;
                    break;

                default:
                    state = START;
                    break;
            }
        }

        // Send UA to confirm termination
        printf(YELLOW "Sending UA confirmation...\n" RESET);
        buildSupervisionFrame(role, UA, frame);
        writeBytesSerialPort(frame, 5);

    } else if (role == LlRx) {

        // Wait for DISC from transmitter using state machine
        state = START;
        while (!STOP) {
            int bytesRead = readByteSerialPort(&readByte);
            if (bytesRead <= 0) continue;

            switch (state) {
                case START:
                    if (readByte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (readByte == ADDRESS_SENDER) { state = A_RCV; bcc = readByte; }
                    else if (readByte != FLAG) state = START;
                    break;

                case A_RCV:
                    if (readByte == DISC) { state = C_RCV; bcc ^= readByte; }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case C_RCV:
                    if (readByte == bcc) state = BCC1_OK;
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case BCC1_OK:
                    if (readByte == FLAG) {
                        printf(GREEN "DISC received from transmitter\n" RESET);
                        STOP = TRUE;
                    } else state = START;
                    break;

                default:
                    state = START;
                    break;
            }
        }

        // Send DISC back
        printf(YELLOW "Sending DISC reply...\n" RESET);
        buildSupervisionFrame(role, DISC, frame);
        writeBytesSerialPort(frame, 5);

        // Wait for UA from transmitter
        STOP = FALSE;
        state = START;
        while (!STOP) {
            int bytesRead = readByteSerialPort(&readByte);
            if (bytesRead <= 0) continue;

            switch (state) {
                case START:
                    if (readByte == FLAG) state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (readByte == ADDRESS_SENDER) { state = A_RCV; bcc = readByte; }
                    else if (readByte != FLAG) state = START;
                    break;

                case A_RCV:
                    if (readByte == UA) { state = C_RCV; bcc ^= readByte; }
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case C_RCV:
                    if (readByte == bcc) state = BCC1_OK;
                    else if (readByte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;

                case BCC1_OK:
                    if (readByte == FLAG) {
                        printf(GREEN "UA received from transmitter\n" RESET);
                        STOP = TRUE;
                    } else state = START;
                    break;

                default:
                    state = START;
                    break;
            }
        }
    }

    if (closeSerialPort() < 0) {
        perror("closeSerialPort");
        return -1;
    }

    printf(GREEN "Serial port closed, link terminated\n" RESET);
    return 0;
}




int buildInformationFrame(const unsigned char *payload, int payloadSize, unsigned char *frame) {
    if (payload == NULL || frame == NULL || payloadSize < 0) {
        return -1;
    }

    printf(YELLOW "llwrite: Building I-frame for %d bytes\n" RESET, payloadSize);

    int frameIndex = 0;

    // 1. Calculate BCC2 (protection of data field)
    unsigned char bcc2 = payload[0];
    for(int i = 1; i < payloadSize; i++) {
        bcc2 ^= payload[i];
    }

    // Start building frame
    // Start flag
    frame[frameIndex++] = FLAG;

    // Address field - commands sent by transmitter
    frame[frameIndex++] = ADDRESS_SENDER;

    // Control field with sequence number N(s) in bit 6
    unsigned char control = (Ns == 0) ? 0x00 : 0x80;
    frame[frameIndex++] = control;

    // BCC1 = A ^ C (header protection)
    frame[frameIndex++] = ADDRESS_SENDER ^ control;

    // Data field with byte stuffing
    for(int i = 0; i < payloadSize; i++) {
        if(payload[i] == FLAG || payload[i] == ESC) {
            frame[frameIndex++] = ESC;
            frame[frameIndex++] = payload[i] ^ XOR_VALUE;
        } else {
            frame[frameIndex++] = payload[i];
        }
    }

    // Add BCC2 with byte stuffing if needed
    if(bcc2 == FLAG || bcc2 == ESC) {
        frame[frameIndex++] = ESC;
        frame[frameIndex++] = bcc2 ^ XOR_VALUE;
    } else {
        frame[frameIndex++] = bcc2;
    }

    // End flag
    frame[frameIndex++] = FLAG;

    return frameIndex;  // Return total frame size
}

// Build a 5-byte supervision frame (F | A | C | BCC1 | F).
// - role: which role is sending the frame (LlTx or LlRx).
// - control: the control byte (SET, UA, RR0/RR1, REJ0/REJ1, DISC ...)
// - frame: output buffer, must have space for at least 5 bytes.
// Returns frame length (5) on success, -1 on invalid args.
int buildSupervisionFrame(LinkLayerRole role, unsigned char control, unsigned char *frame) {
    if (frame == NULL) return -1;

    unsigned char address = (role == LlTx) ? ADDRESS_SENDER : ADDRESS_RECEIVER;

    int idx = 0;
    frame[idx++] = FLAG;
    frame[idx++] = address;
    frame[idx++] = control;
    frame[idx++] = (unsigned char)(address ^ control); // BCC1
    frame[idx++] = FLAG;

    return idx; // should be 5
}

void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    attempts++;
}
