#define  BSD                // WIN for Winsock and BSD for BSD sockets

//----- Include files ---------------------------------------------------------
#include <stdio.h>          // Needed for printf()
#include <string.h>         // Needed for memcpy() and strcpy()
#include <stdlib.h>         // Needed for exit()
#include <fcntl.h>          // Needed for file i/o constants
#include <string.h>         // For the string functions
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
  #include <netdb.h>        // Needed for sockets stuffport
  #include <stdio.h>       // Needed for open(), close(), and eof()
  #include <sys/stat.h>     // Needed for file i/o constants
  #include <sys/select.h> 
  #include <unistd.h>
#endif

//----- Defines ---------------------------------------------------------------
#define  PORT_NUM   6039            // Arbitrary port number for the server
#define  SIZE       8192            // Buffer size 16392
#define  RECV_FILE  "recvFile.dat"  // File name of received file

//----- Structs ---------------------------------------------------------------
struct Packet //The data packet
{
   int type;
   int sqNum;
   int length;
   char data[SIZE];
};
  
struct ACK //The ACK packet
{
   int type;
   int ackNum;
};

//----- Prototypes ------------------------------------------------------------
int recvFile(char *fileName, int portNum, int maxSize, int options); //Receiving File
struct ACK createACKPacket (int ack_type, int base); //Creating Packet
void DieWithError(char *errorMessage); //For errors 

//===== Main program ==========================================================
int main()
{
  int                  portNum;         // Port number to receive on
  int                  maxSize;         // Maximum allowed size of file
  int                  timeOut;         // Timeout in seconds
  int                  options;         // Options
  int                  retcode;         // Return code

  // Initialize parameters
  portNum = PORT_NUM;
  maxSize = 0;     // This parameter is unused in this implementation
  options = 0;     // This parameter is unused in this implementation

  // Receive the file
  printf("Starting file receive... \n");
  retcode = recvFile(RECV_FILE, portNum, maxSize, options);
  printf("File receive is complete \n");

  // Return
  return(0);
}

int recvFile(char *fileName, int portNum, int maxSize, int options)
{
  
#ifdef WIN
  WORD wVersionRequested = MAKEWORD(1,1);       // Stuff for WSA functions
  WSADATA wsaData;                              // Stuff for WSA functions
#endif
  int                  welcome_s;       // Welcome socket descriptor
  struct sockaddr_in   server_addr;     // Server Internet address
  int                  connect_s;       // Connection socket descriptor
  struct sockaddr_in   client_addr;     // Client Internet address
  struct in_addr       client_ip_addr;  // Client IP address
  int                  addr_len;        // Internet address length
  char                 in_buf[SIZE];    // Input buffer for data
  char                 out_buf[SIZE];   // Output buffer for data
  int                  fh;              // File handle
  int                  length;          // Length in received buffer
  int                  retcode;         // Return code
  int                  s_socket;        // Used for the select to wait for recfrom()
  struct timeval       timeout;         // For timeout of the select statement 
  fd_set               recvsds;         // Not entirely sure this is from the example file for the timeout
  int                  base = -2;       // Base number
  int                  seqNumber = 0;   //Sequence number
  

#ifdef WIN
  // This stuff initializes winsock
  WSAStartup(wVersionRequested, &wsaData);
#endif

  // Create a welcome socket
  welcome_s = socket(AF_INET, SOCK_DGRAM, 0);
  if (welcome_s < 0)
  {
    printf("*** ERROR - socket() failed \n");
    exit(-1);
  }

  // Fill-in server (my) address information and bind the welcome socket
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(portNum);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  retcode = bind(welcome_s, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (retcode < 0)
  {
    printf("*** ERROR - bind() failed \n");
    exit(-1);
  }


  // Open IN_FILE for file to write
  #ifdef WIN
    fh = open(fileName, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IREAD | S_IWRITE);
  #endif
  #ifdef BSD
    fh = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE);
  #endif
  if (fh == -1)
  {
     printf("  *** ERROR - unable to create '%s' \n", RECV_FILE);
     exit(1);
  }
  for(;;)
  {  
     addr_len = sizeof(client_addr);
     
     struct Packet dataPacket;
     
     struct ACK ack;
     
     if((length = recvfrom(welcome_s, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr*)&client_addr, &addr_len))<0)
     {
        printf("Recvfrom failed");
        exit(-1);
     }
     
     seqNumber = dataPacket.sqNum;
     
     if(dataPacket.sqNum == 0 && dataPacket.type == 1)
     {
        memset(in_buf, 0, sizeof(in_buf));
        strcpy(in_buf, dataPacket.data);
        write(fh, in_buf, (sizeof(in_buf)-1));
        base = 0;
        ack = createACKPacket(2, base);
     } 
     else if (dataPacket.sqNum == base + 1) //If base+1 then its a subsequent in order packet
     {
        //Then concatinate the data sent to the recieving buffer
        printf("Recieved  Subseqent Packet #%d\n", dataPacket.sqNum);
        strcpy(in_buf, dataPacket.data);
        write(fh, in_buf, (sizeof(in_buf)-1));
        base = dataPacket.sqNum;
        ack = createACKPacket(2, base);
     } 
     else if (dataPacket.type == 1 && dataPacket.sqNum != base + 1)
     {
        //if recieved out of sync packet, send ACK with old base
        printf("Recieved Out of Sync Packet #%d\n", dataPacket.sqNum);
        //Resend ACK with old base
        ack = createACKPacket(2, base);
     }

     //Type 4 means that the packet recieved is a termination packet
     if(dataPacket.type == 4 && seqNumber == base )
     {
         base = -1;
         //create an ACK packet with terminal type 8
         ack = createACKPacket(8, base);
     }

     //Send ACK for Packet Recieved
     if(base >= 0)
     {
         printf("------------------------------------  Sending ACK #%d\n", base);
         if (sendto(welcome_s, &ack, sizeof(ack), 0, (struct sockaddr *) &client_addr, sizeof(client_addr)) != sizeof(ack))
            DieWithError("sendto() sent a different number of bytes than expected");
     } 
     else if (base == -1) 
     {
         printf("Recieved Teardown Packet\n");
         printf("Sending Terminal ACK\n", base);
         if (sendto(welcome_s, &ack, sizeof(ack), 0, (struct sockaddr *) &client_addr, sizeof(client_addr)) != sizeof(ack))
            DieWithError("sendto(2) sent a different number of bytes than expected");
         break;
     }
     
     if(dataPacket.type == 4 && base == -1)
     {
                memset(in_buf, 0, sizeof(in_buf));
     }
      
   }

  // Close the file that was sent to the receiver
  close(fh);   
  

  // Close the welcome socket
#ifdef WIN
  retcode = closesocket(welcome_s);
  if (retcode < 0)
  {
    printf("*** ERROR - closesocket() failed \n");
    exit(-1);
  }
#endif
#ifdef BSD
  retcode = close(welcome_s);
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


//Creates ACK packet
struct ACK createACKPacket (int ack_type, int base){
        struct ACK ack;
        ack.type = ack_type;
        ack.ackNum = base;
        return ack;
}

//Handles errors
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}