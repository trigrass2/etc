/**********************************************************************
   udprecv.c : UDP receive program
   Jan 6,2001  copyright Takeshi FUJIKI (fujiki@fc-lab.com)
***********************************************************************/
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define WAIT_PORT 8000
#define BUFLEN 256

main(int argc, char *argv[]) {
  int udp_socket;
  struct sockaddr_in my_addr, from_addr;
  int flag, n, from_len;
  char buf[BUFLEN];

  udp_socket = socket(AF_INET, SOCK_DGRAM,0);

  bzero((char *) &from_addr,sizeof(from_addr));
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  my_addr.sin_port = htons(WAIT_PORT);

  bind(udp_socket, (struct sockaddr *) &my_addr, sizeof(my_addr));

  flag=0;
  while(1) {
    n = recvfrom(udp_socket,buf,BUFLEN,flag,&from_addr,&from_len);
    write(1,buf,n);
  }
}
