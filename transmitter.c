/* File 	: transmitter.c */
#include "List/list2.h"

/* NETWORKS */
int sockfd, port;		// sock file descriptor and port number
struct hostent *server;
struct sockaddr_in receiverAddr;			// receiver host information
int receiverAddrLen = sizeof(receiverAddr);

/* FILE AND BUFFERS */
FILE *tFile;			// file descriptor
char buf[BUFMAX];		// buffer for read characters

/* WINDOW */ 
MESGB rxbuf[WINDOWSIZE];
QTYPE trmq = { 0, 0, 0, WINDOWSIZE, rxbuf};
QTYPE *rxq = &trmq;

/* SEND WINDOW */
MESGB rxsend[WINDOWSIZE];
QTYPE trsend = { 0, 0, 0, WINDOWSIZE, rxsend};
QTYPE *rxnd = &trsend;

/* QTEMP */
List temp;
List *ptemp = &temp;

/* FLAGS */
int isSocketOpen;	// flag to indicate if connection from socket is done

/* VOIDS */
void *firstChild(void *threadid);
void *secondChild(void *threadid);
void receiveACK(QTYPE *queue,QTYPE *qsend,List *temp);
void sendFrame(QTYPE *qsend);

int main(int argc, char *argv[]) {
	pthread_t thread[2];

	if (argc < 4) {
		// case if arguments are less than specified
		printf("Please use the program with arguments: %s <target-ip> <port> <filename>\n", argv[0]);
		return 0;
	}

	if ((server = gethostbyname(argv[1])) == NULL)
		error("ERROR: Receiver Address is Unknown or Wrong.\n");

	// creating IPv4 data stream socket
	printf("Creating socket to %s Port %s...\n", argv[1], argv[2]);
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		error("ERROR: Create socket failed.\n");

	// flag set to 1 (connection is established)
	isSocketOpen = 1;

	// initializing the socket host information
	memset(&receiverAddr, 0, sizeof(receiverAddr));
	receiverAddr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&receiverAddr.sin_addr.s_addr, server->h_length);
	receiverAddr.sin_port = htons(atoi(argv[2]));

	// open the text file
	tFile = fopen(argv[argc-1], "r");
	if(tFile == NULL) 
		error("ERROR: File text not Found.\n");

	// sending frame thread
	if(pthread_create(&thread[0], NULL, firstChild, 0) != 0) 
		error("ERROR: Failed to create thread for child. Please free some space.\n");

	// receiving ack thread
	CreateList(ptemp); //make list for collecting ack
	if(pthread_create(&thread[1], NULL, secondChild, 0) != 0) 
		error("ERROR: Failed to create thread for child. Please free some space.\n");

	// parent process
	int counter = 0;
	while(1) {
		buf[0] = fgetc(tFile);
		MESGB msg = { .soh = SOH, .stx = STX, .etx = ETX, .checksum = 0, .msgno = counter++};
		initiateCksum(&msg);
		printf("cksum %d\n",msg.checksum);
		strcpy(msg.data, buf);
		while(trmq.count==WINDOWSIZE); /*wait for sending process */
 		trmq.window[trmq.rear] = msg;
 		trmq.rear++;
 		if(trmq.rear==WINDOWSIZE) trmq.rear=0;
 		trmq.count++;
 		//adding to queue send
 		while(trsend.count==WINDOWSIZE);
 		trsend.window[trsend.rear] = msg;
 		trsend.rear++;
 		if(trsend.rear==WINDOWSIZE) trsend.rear=0;
 		trsend.count++;
		sleep(DELAY);
		if(buf[0]==EOF) break;
	}

	for (int i = 0; i < 2; i++)
       pthread_join(thread[i], NULL);

	// sending endfile to receiver, marking the end of data transfer
	printf("Sending EOF");
	fclose(tFile);
	
	printf("Frame sending done! Closing sockets...\n");
	close(sockfd);
	isSocketOpen = 0;
	printf("Socket Closed!\n");
	return 0;
}

void error(const char *message) {
	perror(message);
	exit(1);
}

void *firstChild(void *threadid) {
	//this thread used for sending frame process
	while(1) {
		sendFrame(rxnd);
		sleep(DELAY);
	}
	pthread_exit(NULL);
}

void sendFrame(QTYPE *qsend) {
	//child process for sending frame
	char string[128];
	if(qsend->count) {
		printf("Sending frame no. %d: \'%c\'\n", qsend->window[qsend->front].msgno, qsend->window[qsend->front].data[0]);
		memcpy(string,&qsend->window[qsend->front],sizeof(MESGB));
		if(sendto(sockfd, string, sizeof(MESGB), 0, (const struct sockaddr *) &receiverAddr, receiverAddrLen) == sizeof(MESGB)) {
			qsend->front++;
			if(qsend->front==WINDOWSIZE) qsend->front=0;
			qsend->count--;
		}
		else error("ERROR: sendto() sent frame with size more than expected.\n");
	}
}

void *secondChild(void *threadid) {
	//this thread used for receiving ack process
	while(1) {
		receiveACK(rxq,rxnd,ptemp);
	}
	pthread_exit(NULL);	
}

void receiveACK(QTYPE *queue,QTYPE *qsend, List *temp) {
	//child process for receiving ack
	struct sockaddr_in srcAddr;
	int srcLen = sizeof(srcAddr);
	char string[128];
	RESPL *rsp = (RESPL *) malloc(sizeof(RESPL));
	int wait=1;
	while (isSocketOpen && wait<1000 && wait>0) {
		if(recvfrom(sockfd, string, sizeof(RESPL), 0, (struct sockaddr *) &srcAddr, &srcLen) == sizeof(RESPL))
			wait=-1;
		else 
			error("ERROR: Failed to receive frame from socket.\n");
		memcpy(rsp,string,sizeof(RESPL));
		printf("receive frame %d\n", rsp->msgno);
		if(rsp->msgno == queue->window[queue->front].msgno) {
			if(rsp->ack == ACK) {				
				queue->front++; //inc *queue head
				if(queue->front == WINDOWSIZE) queue->front = 0;
				queue->count--;
			}
			else {				
 				//adding to queue send
 				while(qsend->count==WINDOWSIZE);
 				qsend->window[qsend->rear] = queue->window[queue->front];
 				qsend->rear++;
 				if(qsend->rear==WINDOWSIZE) qsend->rear=0;
 				qsend->count++;
			}
		}
		else { 
			address P = Search(*ptemp,rsp->msgno);
			if(P!=Nil) { //if ack with same msgno found in ptemp
				if(Info(P)->ack == ACK) {					
					queue->front++; //inc *queue head
					if(queue->front == WINDOWSIZE) queue->front = 0;
					queue->count--;
				}
				else { //we have to resend the frame  					
 					while(qsend->count==WINDOWSIZE);
 					qsend->window[qsend->rear] = queue->window[queue->front]; //add it to send queue
 					qsend->rear++;
 					if(qsend->rear==WINDOWSIZE) qsend->rear=0;
 					qsend->count++;					
				}
				address Pdel;
				DelAfter(ptemp,&Pdel,Prev(P));
			}
			else InsVLast(ptemp,rsp); //save in ptemp
		}
		usleep(DELAY);
		wait++;
		printf("waiting for ACK\n"); 
	}
	if(wait) {
		//adding to queue send
		printf("recv timeout, resending head\n");
		while(qsend->count==WINDOWSIZE);
		qsend->window[qsend->rear] = queue->window[queue->front];
		qsend->rear++;
		if(qsend->rear==WINDOWSIZE) qsend->rear=0;
		qsend->count++;	
	}
}