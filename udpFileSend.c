#define  BSD                // WIN for Winsock and BSD for BSD sockets

//----- Include files ---------------------------------------------------------
#include <stdio.h>          // Needed for printf()
#include <string.h>         // Needed for memcpy() and strcpy()
#include <stdlib.h>         // Needed for exit()
#include <fcntl.h>          // Needed for file i/o constants                        
#include <string.h>                                                                 
#include <ctype.h>                                                                  
#ifdef WIN                                                                          
  #include <windows.h>      // Needed for all Winsock stuff
  #include <io.h>             // Needed for open(), close(), and eof()
  #include <sys\stat.h>       // Needed for file i/o constants
#endif
#ifdef BSD
  #include <sys/types.h>    // Needed for sockets stuff
  #include <netinet/in.h>   // Needed for sockets stuff
  #include <sys/socket.h>   // Needed for sockets stuff
  #include <arpa/inet.h>    // Needed for sockets stuff
  #include <fcntl.h>        // Needed for sockets stuff
  #include <netdb.h>        // Needed for sockets stuff
  #include <stdio.h>        // Needed for open(), close(), and eof()
  #include <sys/stat.h>     // Needed for file i/o constants
  #include <unistd.h>       // 
  #include <errno.h>        // Needed for alarm
  #include <signal.h>       // Needed for alarm
#endif

//----- Defines ---------------------------------------------------------------
#define  PORT_NUM       6039    // Port number used at the server
#define  SIZE           8192    // Buffer size 16392
#define  TIMEOUT_SECS    2

//----- Structs ---------------------------------------------------------------
struct Packet
{
   int type;
   int sqNum;
   int length;
   char data[SIZE];
};
  
struct ACK
{
   int type;
   int ackNum;
};

//----- Prototypes ------------------------------------------------------------
int sendFile(char *fileName, char *destIpAddr, int destPortNum, int options);
struct Packet createDataPacket (int seq_no, int length, char* data);
struct Packet createTerminalPacket (int seq_no, int length);
void DieWithError(char *errorMessage);
void CatchAlarm(int ignored);

//===== Main program ==========================================================
int main(int argc, char *argv[])
{
  char                 sendFileName[256];   // Send file name
  char                 recv_ipAddr[16];     // Reciver IP address
  int                  recv_port;           // Receiver port number
  int                  options;             // Options
  int                  retcode;             // Return code

  // Usage and parsing command line arguments
  if (argc != 4)
  {
    printf("usage: 'projectServer sendFile recvIpAddr recvPort' where      \n");
    printf("       sendFile is the filename of an existing file to be sent \n");
    printf("       to the receiver, recvIpAddr is the IP address of the    \n");
    printf("       receiver, and recvPort is the port number for the       \n");
    printf("       receiver where tcpFileRecv is running.                  \n");
    return(0);
  }
  strcpy(sendFileName, argv[1]);
  strcpy(recv_ipAddr, argv[2]);
  recv_port = atoi(argv[3]);

  // Initialize parameters
  options = 0;     // This parameter is unused in this implementation

  // Send the file
  printf("Starting file transfer... \n");
  retcode = sendFile(sendFileName, recv_ipAddr, recv_port, options);
  printf("File transfer is complete \n");

  // Return
  return(0);
}


int sendFile(char *fileName, char *destIpAddr, int destPortNum, int options)
{
  
#ifdef WIN
  WORD wVersionRequested = MAKEWORD(1,1);       // Stuff for WSA functions
  WSADATA wsaData;                              // Stuff for WSA functions
#endif
  int                  client_s;           // Client socket descriptor
  struct sockaddr_in   server_addr;        // Server Internet address
  int                  addr_len;           // Internet address length
  char                 out_buf[SIZE]; // Output buffer for data
  char                 in_buf[SIZE];  // Input buffer for data
  int                  fh;                 // File handle
  int                  length = 1;         // Length of send buffer
  int                  retcode;         // Return code
  unsigned long int    noBlock;         // Non-blocking flag
  struct timeval       timeout;         // For timeout of the select statement 
  fd_set               recvsds;         // Not entirely sure this is from the example file for the timeout
  int                  timeoutResend;   // For resending if it timesout too long
  int                  s_socket;        // Used for the select to wait for recfrom()
  int                  seqNumber = 0;   // Sequence number
  int                  base = -1;       // Base Number
  int                  noTearDownACK = 1; // Sent to stop sender
  int                  numOfSegments = 0; // # of segments in the file
  int                  windowSize = 8;    // Window Size
  int                  tries = 0;         // Tries until it stops trying to resend
  struct sigaction     myAction;          // For the alarm
  

#ifdef WIN
  // This stuff initializes winsock
  WSAStartup(wVersionRequested, &wsaData);
#endif

  // Create a client socket
  client_s = socket(AF_INET, SOCK_DGRAM, 0);
  if (client_s < 0)
  {
    printf("*** ERROR - socket() failed \n");
    exit(-1);
  }

  // Fill-in the server's address information and do a connect
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT_NUM);
  server_addr.sin_addr.s_addr = inet_addr(destIpAddr);
  

  // Open file to send
  #ifdef WIN
    fh = open(fileName, O_RDONLY | O_BINARY, S_IREAD | S_IWRITE);
  #endif
  #ifdef BSD
    fh = open(fileName, O_RDONLY, S_IREAD | S_IWRITE);
  #endif
  if (fh == -1)
  {
     printf("  *** ERROR - unable to open '%s' \n", sendFile);
     exit(1);
  }
  
  //get number of segments for the file
  while(length!=0)
  {
      length = read(fh, out_buf, SIZE);
      numOfSegments++;
  }
  
  // Close the file that was sent to the receiver
  close(fh);
  
  // Open file to send
  #ifdef WIN
    fh = open(fileName, O_RDONLY | O_BINARY, S_IREAD | S_IWRITE);
  #endif
  #ifdef BSD
    fh = open(fileName, O_RDONLY, S_IREAD | S_IWRITE);
  #endif
  if (fh == -1)
  {
     printf("  *** ERROR - unable to open '%s' \n", sendFile);
     exit(1);
  }
  
  addr_len = sizeof(server_addr);
  
  //et signal handler for alarm signal
    myAction.sa_handler = CatchAlarm;
    if (sigemptyset(&myAction.sa_mask) < 0) //block everything in handler
        DieWithError("sigfillset() failed");
    myAction.sa_flags = 0;

    if (sigaction(SIGALRM, &myAction, 0) < 0)
        DieWithError("sigaction() failed for SIGALRM");

      
  while(noTearDownACK)
  {
     //Send chunks from base up to window size
     while(seqNumber <= numOfSegments && (seqNumber - base) <= windowSize)
     {
         struct Packet dataPacket;

         if(seqNumber == numOfSegments)
         {
                //Reached end, create terminal packet
                dataPacket = createTerminalPacket(seqNumber, 0);
                printf("Sending Terminal Packet\n");
         } 
         else 
         {
                length = read(fh, out_buf, SIZE);
                dataPacket = createDataPacket(seqNumber, length, out_buf);
                printf("Sending Packet: %d\n", seqNumber);
                //printf("Chunk: %s\n", seg_data);
         }

         //Send the constructed data packet to the receiver
         if (sendto(client_s, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr *) &server_addr, sizeof(server_addr)) != sizeof(dataPacket))
                DieWithError("sendto() sent a different number of bytes than expected");
         seqNumber++;
     }
  
    alarm(TIMEOUT_SECS);
  
    struct ACK ack;
    while ((length = recvfrom(client_s, &ack, sizeof(ack), 0, (struct sockaddr *) &server_addr, &addr_len)) < 0)
        {
            if (errno == EINTR)     /* Alarm went off  */
            {
                //reset the seqNumber back to one ahead of the last recieved ACK
                seqNumber = base + 1;

                printf("Timeout: Resending\n");
                if(tries >= 10){
                    printf("Tries exceeded: Closing\n");
                    exit(1);
            }
            else 
            {
                alarm(0);

                while(seqNumber <= numOfSegments && (seqNumber - base) <= windowSize){
                struct Packet dataPacket;

                if(seqNumber == numOfSegments)
                {
                     //Reached end, create terminal packet
                     dataPacket = createTerminalPacket(seqNumber, 0);
                     printf("Sending Terminal Packet\n");
                }
                else
                {
                     length = read(fh, out_buf, SIZE);
                     dataPacket = createDataPacket(seqNumber, length, out_buf);
                     printf("Sending Packet: %d\n", seqNumber);
                }

                      //Send the constructed data packet to the receiver
                     if (sendto(client_s, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr *) &server_addr, sizeof(server_addr)) != sizeof(dataPacket))
                        DieWithError("sendto() sent a different number of bytes than expected");
                     seqNumber++;
                    }
                    alarm(TIMEOUT_SECS);
                }
                tries++;
            }
            else
            {
                DieWithError("recvfrom() failed");
            }
        }

        //8 is teardown ack
        if(ack.type != 8)
        {
            printf("----------------------- Recieved ACK: %d\n", ack.ackNum);
            if(ack.ackNum>base)
            {
                //Advances the sending, reset tries
                base = ack.ackNum;
            }
        } 
        else 
        {
            printf("Recieved Terminal ACK\n");
            noTearDownACK = 0;
        }

        //recvfrom() got something --  cancel the timeout, reset tries
        alarm(0);
        tries = 0;

    }
  


  // Close the file that was sent to the receiver
  close(fh);
  
  // Close the client socket
#ifdef WIN
  retcode = closesocket(client_s);
  if (retcode < 0)
  {
    printf("*** ERROR - closesocket() failed \n");
    exit(-1);
  }
#endif
#ifdef BSD
  retcode = close(client_s);
  if (retcode < 0)
  {
    printf("*** ERROR - close() failed \n");
    exit(-1);
  }
#endif

#ifdef WIN
  // Clean-up winsock
  WSACleanup();
#endif

  // Return zero
  return(0);
}



//Create Packet
struct Packet createDataPacket (int sqNum, int length, char* data)
{
    struct Packet pkt;

    pkt.type = 1;
    pkt.sqNum = sqNum;
    pkt.length = length;
    memset(pkt.data, 0, sizeof(pkt.data));
    strcpy(pkt.data, data);

    return pkt;
}

//Create Terminal Packet
struct Packet createTerminalPacket (int sqNum, int length)
{

    struct Packet pkt;

    pkt.type = 4;
    pkt.sqNum = sqNum;
    pkt.length = 0;
    memset(pkt.data, 0, sizeof(pkt.data));

    return pkt;
}

//Handles Errors
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

//For the alarm
void CatchAlarm(int ignored)     /* Handler for SIGALRM */
{
    //printf("In Alarm\n");
}