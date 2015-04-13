/*
* Author: James Kerivan
* Class: CIS 432
* Fall 2014
* Client.cpp
* Programming Assignment #1
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <set>
#include <string>

#include "duckchat.h"
#include "raw.h"

#define BUFFSIZE 1024
#define true 1
#define false 0

/* Global Variables Used*/
int socketfd = 0;
struct addrinfo * servinfo = NULL;
struct sockaddr_in * fromAddr;
socklen_t fromAddrLen = sizeof(fromAddr);

/* Default Channel "Common", also active channel */
const char mainChannel[CHANNEL_MAX] = "Common";
char liveChannel[CHANNEL_MAX];

/* Recv Buffer */ 
char inBuffer[SAY_MAX + 1];
char *buffPosition = inBuffer;

std::set<std::string> channelSet;


/* Function Declarations */
void promptUser();
void clearPrompt();
int logout();
int join(const char*);
int leave(const char*);
int say(const char * msg);
int list();
int login(const char*);
int handleSwitch(const char*);
int who(const char*);
char * inputString();
int parseInput(char *);
int printError(const char*);
int sendMsg(struct request*, int);
int buildSocket(char*, char*);
int switchResp(struct text*);


 
int main(int argc, char *argv[]) {
   
	/* handle arguments */
	if(argc != 4) {
		printf("Incorrect arguments, please use:\n./client server port username\n");
		return 1;
	}

	raw_mode(); 

	memset(liveChannel, '\0', CHANNEL_MAX);
	char * address = argv[1];
	char * port = argv[2];
	char * username = argv[3];

	/* Build Socket we pass the function the address and port */
	if(buildSocket(address, port) != true) {
		cooked_mode();
		return 1;
	}

	/* Handle Login */
	if(login(username) != true) {
		cooked_mode();
		return 1;
	}

	int parseStatus = true;
	char * input;
	fd_set readfds;

	promptUser();

	do {
		FD_ZERO(&readfds);
		FD_SET(0, &readfds);
		FD_SET(socketfd, &readfds);
		select(socketfd+1, &readfds, NULL, NULL, NULL);

		if(FD_ISSET(0, &readfds)) {

			input = inputString();


			if(input != NULL) {

				/* Parse Input */

			
				parseStatus = parseInput(input);

				if(parseStatus != -1) { 
					promptUser();
				}
			}

		} else if (FD_ISSET(socketfd, &readfds)) { 

			struct text * text = (struct text *) malloc(sizeof(struct text) + 1024);
			int bytes = 0;

			if((bytes = recvfrom(socketfd, text, 1024, 0,servinfo->ai_addr, &servinfo->ai_addrlen)) > 0) {
			
				clearPrompt();
				switchResp(text);
				free(text);
			
			}

		}	

	} while(parseStatus != -1);
	
	freeaddrinfo(servinfo);

	cooked_mode();
	return 0;
}

int buildSocket(char * address, char * port) {

	int result = 0;

	/* Hints */
	struct addrinfo hints;
	struct addrinfo *p;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	/* Get address info structure */
	if((result = getaddrinfo(address, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n",gai_strerror(result));
		return false;
	}

	/* Create the UDP socket */
	for(p = servinfo; p != NULL; p = p -> ai_next) {
		socketfd = socket(p -> ai_family, p -> ai_socktype, p -> ai_protocol);
	
		if(socketfd == -1) {
			perror("Socket File Descriptor");
			continue;
		}
		break;
	}

	servinfo = p;

	return(p != NULL);
}

int login(const char * username) {

	struct request_login * request = (struct request_login*) malloc(sizeof(struct request_login));
	request -> req_type = htonl(REQ_LOGIN);
	strncpy(request -> req_username, username, USERNAME_MAX);
	
	int result = sendMsg((struct request*) request, sizeof(struct request_login));

	free(request);

	return result && join(mainChannel);;
}

void promptUser() {

	printf("> ");
	fflush(stdout);
}

void clearPrompt() {
	printf("\b\b");
	fflush(stdout);
}

int join(const char * channel) {

	if(channel == NULL) {
		printError("/join <channel>\n");
		return false;
	}

	struct request_join * request = (struct request_join*) malloc(sizeof(struct request_join));
	request-> req_type = 2;
	strncpy(request->req_channel, channel, CHANNEL_MAX);

	int result = sendMsg((struct request *) request, sizeof(struct request_join));

	
	//add to channel set
	if(result == true) {
		std::string str = channel;
		channelSet.insert(str);	
	}
	strncpy(liveChannel, channel, CHANNEL_MAX);

	free(request);
	return result;
}

int sendMsg(struct request * req, int len) {

	int result = sendto(socketfd, req, len, 0, servinfo -> ai_addr, servinfo -> ai_addrlen);
	if(result == -1) {
		return false;
	}
	return true;
}

int leave(const char * channel) {

	if(channel == NULL) {
		printError("/leave <channel>\n");
		return false;
	}

	struct request_leave *request = (struct request_leave*)malloc(sizeof(struct request_leave));
	request -> req_type = 3;
	strncpy(request -> req_channel, channel, CHANNEL_MAX);

	int result = sendMsg((struct request *) request, sizeof(struct request_leave));
	

	//delete from channel set
	if(result == true) {
		std::string str = channel;
		channelSet.erase(str);
	}

	free(request);
	return result;
}

int parseInput(char * input) {
	
	const char * delim = " \n";
	char * tok = (char*) malloc(sizeof(char)*(strlen(input) + 1));

	// copy input string to tok
	strcpy(tok, input);
	
	// get commands using strok and delim
	char * getCommand = strtok(tok, delim);


	if(getCommand == NULL) {
		return false;
	}

	int result = false;

	// find what command and then call proper function
	if( 0 == strcmp(getCommand, "/join")) {
		char * channel = strtok(NULL, delim);
		result = join(channel);
	} else if ( 0 == strcmp(getCommand, "/leave")) {
		char * channel = strtok(NULL, delim);
		result = leave(channel);
	} else if ( 0 == strcmp(getCommand, "/who")) {
		char * channel = strtok(NULL, delim);
		result = who(channel);
	} else if ( 0 == strcmp(getCommand, "/list")) {
		result = list();
	} else if ( 0 == strcmp(getCommand, "/exit")) {
		result = logout();
	} else if ( 0 == strcmp(getCommand, "/switch")) {
		char * channel = strtok(NULL, delim);
		result = handleSwitch(channel);
	} else { // this is where /say is handled
		result = say(input);
	}
	
	free(tok);
	return result;
}

char * inputString() {
	
	char c = getchar();

	if(c == '\n') { //newline
		*buffPosition++= '\0';
		buffPosition = inBuffer;
		printf("\n");
		fflush(stdout);
		return inBuffer;

	} else if (((int)c) == 127) { //backspace
		
		if(buffPosition > inBuffer) {
			--buffPosition;
			printf("\b");
			fflush(stdout);
		}

	} else if (buffPosition != inBuffer + SAY_MAX) {
		*buffPosition++ = c;
		printf("%c", c);
		fflush(stdout);
		return NULL;
	}
	return NULL;
}

int who(const char * channel) {
	
	if(channel == NULL) {
		printError("/who <channel>\n");
		return false;
	}

	struct request_who * request = (struct request_who *) malloc(sizeof(struct request_who));
	request -> req_type = 6;
	strncpy(request -> req_channel, channel, CHANNEL_MAX);
	
	int result = sendMsg((struct request *) request, sizeof(struct request_who));
	free(request);
	return result;
}

int say(const char * message) {

	if(message == NULL || strcmp(liveChannel, "") == 0) {
		return false;
	}
	struct request_say * request = (struct request_say *) malloc(sizeof(struct request_say));
	request -> req_type = 4;
	strncpy(request->req_channel, liveChannel, CHANNEL_MAX);

	strncpy (request->req_text, message, SAY_MAX);

	int result = sendMsg((struct request *) request, sizeof(struct request_say));

	free(request);
	return result;	
}

int list() {
	
	struct request_list * request = (struct request_list *) malloc(sizeof(struct request_list));
	request -> req_type = 5;
	
	int result = sendMsg((struct request *) request, sizeof(struct request_list));

	free(request);
	return result;
}

int logout() {

	struct request_logout * request = (struct request_logout *) malloc(sizeof(struct request_logout));
	request -> req_type = 1;

	sendMsg((struct request *) request, sizeof(struct request_logout));

	free(request);
	return -1;
}

int recvSay(struct text_say* say) {

	if((buffPosition - inBuffer) > 0) {
		printf("\n");
	}

	printf("[%s][%s]%s\n", say->txt_channel, say->txt_username, say->txt_text);

	promptUser();

	if((buffPosition - inBuffer) > 0) {
		char * cPtr;
		for(cPtr = inBuffer; cPtr < buffPosition; ++cPtr) {
			printf("%c", *cPtr);
		}
	}

	fflush(stdout);
	return true;
}

int recvError(struct text_error * error) {

	printError(error->txt_error);
	printf("\n");
	promptUser();
	return true;
}

int recvList(struct text_list * list) {
	
	int numChannels = ((int)list -> txt_nchannels);

	if((buffPosition - inBuffer) > 0) {
		
		printf("\n");
	}

	printf("Existing Channels: \n");
	
	int i;
	for(i = 0; i < numChannels; i++) {
		printf(" %s\n", list-> txt_channels[i].ch_channel);
	}

	promptUser();

	if((buffPosition - inBuffer) > 0) {
		
		char *cPtr;
		for(cPtr = inBuffer; cPtr < buffPosition; ++cPtr) {
			printf("%c", *cPtr);
		}
	}

	fflush(stdout);
	return true;
}

int handleSwitch(const char * channel) {

	std::set<std::string>::iterator it;
	
	if((it = channelSet.find(channel)) != channelSet.end()) {
		strncpy(liveChannel, it->c_str(), CHANNEL_MAX);
		return true;
	}
	printError("You haven't joined that channel \n");
	return false;
}

int recvWho(struct text_who * whoUsers) {
	
	int numUsers = ((int)whoUsers -> txt_nusernames);

	

	if((buffPosition - inBuffer) > 0) {
		printf("\n");
	}

	printf("Users in channel %s:\n", whoUsers->txt_channel);

	int i;
	for(i = 0; i < numUsers; i++) {
		printf(" %s\n", whoUsers -> txt_users[i].us_username);
	}

	promptUser();

	if((buffPosition - inBuffer) > 0) {
		
		char * cPtr;
		for(cPtr = inBuffer; cPtr < buffPosition; ++cPtr) {
			printf("%c", *cPtr);
		}
	}
	
	fflush(stdout);
	return true;
}

int printError(const char * message) {

	fprintf(stderr, message);
	fflush(stderr);
	return true;
}

int switchResp(struct text * text) {

	switch((int)text->txt_type) {
		case TXT_ERROR:
			return recvError((struct text_error *) text);
		case TXT_LIST:
			return recvList((struct text_list *) text);
		case TXT_WHO:
			return recvWho((struct text_who *) text);
		case TXT_SAY:
			return recvSay((struct text_say *) text);
		default:
			printError("Unknown response type received\n");
			return false;
	}
}
