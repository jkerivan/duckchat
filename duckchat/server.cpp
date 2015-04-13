/*
* James Kerivan
* CIS 432 Networking
* Program 2
*/

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <signal.h>

#include "duckchat.h"

#define true 1
#define false 0
#define BUFSIZE 1024
#define TIMESIZE 100

const int SEND_JOIN_REQUEST_TIME = 60;

int sockfd = 0;
int maxfd= 0;

std::string myHost;
std::string myHostip; 


struct addrinfo *servinfo= NULL;

struct sockaddr_storage lastUser;
socklen_t lastSize= sizeof(lastUser);

std::set<std::string> set_chanList;
std::set<long long> set_sayList;
std::vector<std::string> vec_serverAddrs;
std::map<std::string, std::string> map_addrToUser;
std::map<std::string, std::string> map_userToAddr;
std::multimap<std::string, std::string> map_userToChan;
std::multimap<std::string, std::string> map_chanToUser;
std::multimap<std::string, std::string> map_chanToServer;

char buf[1024];

std::string addrToString(const struct sockaddr_in*);
std::string addrToUser(const struct sockaddr_in*);

void timerExpired(int signum);

int addUser(const char*);
int addUserToChannel(const char*, const char*);
long long genuid();
int msg_error(const char*);
int msg_list();
int msg_s2s_join(const char*);
int msg_s2s_leave(const char*);
int msg_s2s_say(char*, char*, char*);
int msg_say(const char*, const char*, const char*, const struct sockaddr*);
int msg_who(const char*);
struct sockaddr *new_stringToAddr(std::string);
char *new_timeStr();
int recv_join(struct request_join*);
int recv_keepAlive(struct request_keep_alive*);
int recv_leave(struct request_leave*);
int recv_list(struct request_list*);
int recv_login(struct request_login*);
int recv_logout(struct request_logout*);
int recv_say(struct request_say*, long long, char*);
int recv_who(struct request_who*);
int removeLastUser();
int removeUserFromChannel(const char*, const char*);
int s2s_broadcast(struct request*, int);
int s2s_forward(struct request*, int, int);
int s2s_send(const struct sockaddr*, size_t, struct request*, int);
int s2s_sendToLast(struct request*, int, int);
int sendMessage(const struct sockaddr*, size_t, struct text*, int);
int sendToLast(struct text*, int);
int setupSocket(char*, char*);
int switchRequest(struct request*, int);

/*Program execution */
int main(int argc, char **argv) {

	signal(SIGALRM, timerExpired);
	alarm(SEND_JOIN_REQUEST_TIME);

	// correct args
	if (argc % 2 == 0 || argc <= 1){
		printf("%s", "usage: hostname port (adjacenthostname adjacentport)* \n");
		return 1;
	}

	char * format= (char*) malloc(sizeof(char) * BUFSIZE);

	//setup Socket
	setupSocket(argv[1], argv[2]);

	maxfd = sockfd;
	
	//print local to buffer
	sprintf(format, "%s:%s", argv[1], argv[2]);

	//Get local host address ip:port
	myHost= format;
	struct sockaddr_in * localAddr= (struct sockaddr_in*) new_stringToAddr(myHost);
	localAddr->sin_port= ntohs(localAddr->sin_port);
	myHostip = addrToString((struct sockaddr_in*)localAddr);

	//handle input for more hosts and ports
	for (int i= 3; i < argc; i+= 2) {

		char *inetRes= (char*) malloc(sizeof(char) * BUFSIZE);
		sprintf(inetRes, "%s:%s", argv[i], argv[i+1]);
		std::string ip= inetRes;
		struct sockaddr_in * saddr= (struct sockaddr_in*) new_stringToAddr(ip);
		saddr->sin_port= ntohs(saddr->sin_port);
		std::string ip2 = addrToString((struct sockaddr_in*)saddr);
		vec_serverAddrs.push_back(ip2);
	}

	free(format);
	int numbytes= 0;

	while (true) {

		struct request *req= (struct request*) malloc(sizeof (struct request) + BUFSIZE);
		if ((numbytes = recvfrom(sockfd, req, 1024, 0, (struct sockaddr*)&lastUser, &lastSize)) > 0) {
			
			switchRequest(req, numbytes);
		}
		free(req);
	}

	freeaddrinfo(servinfo);
	return 0;
}

//Helps with softstate
void timerExpired(int signum) {

	if(!set_chanList.empty()) {

		for (std::set<std::string>::iterator it=set_chanList.begin(); it!=set_chanList.end(); ++it) {
			std::string channel = *it;
			//printf("%s\n", channel.c_str());
			//get Join Request ready
			struct request_s2s_join *req= (struct request_s2s_join*) malloc(sizeof(struct request_s2s_join));
			req->req_type= ((int)REQ_S2S_JOIN);
	
			if (vec_serverAddrs.size() == 0) {return;}

			for (int i= 0; i < vec_serverAddrs.size(); i++) {
				msg_s2s_join(channel.c_str());
			}
		}
		
	}
	signal(SIGALRM, timerExpired);
	alarm(SEND_JOIN_REQUEST_TIME);
}

//unique ID generator
long long genuid() {

	long long uid= 0LL;
	int fd= open("/dev/urandom", O_RDONLY);

	if (fd == -1) {
		return 0;
	}

	int numbytes= read(fd, &uid, 8);

	if (numbytes == 0) {
		return 0;
	}
	return uid;
}


int sendMessage(const struct sockaddr *addr, size_t addrlen, struct text *msg, int msglen) {

	int result= sendto(sockfd, msg, msglen, 0, addr, addrlen);
	if (result == -1) {
		perror("SendMessage: \n");
		return false;
	}

	return true;
}

int msg_say(const char *channel, const char *fromUser, const char *msg, const struct sockaddr *addr) {

	struct text_say *txt= (struct text_say*) malloc(sizeof(struct text_say));

	std::string addrStr= addrToString((struct sockaddr_in*)&lastUser);

	txt->txt_type=  (int)TXT_SAY;
	strncpy(txt->txt_channel, channel, CHANNEL_MAX);
	strncpy(txt->txt_username, fromUser, USERNAME_MAX);
	strncpy(txt->txt_text, msg, SAY_MAX);
	

	int result= sendMessage(addr, sizeof(struct sockaddr_in), (struct text*)txt, sizeof(struct text_say));

	if(result) {
		

	} 

	free(txt);

	return result;
}

struct sockaddr *new_stringToAddr(std::string straddr) {

	struct sockaddr_in * sa= (struct sockaddr_in*) malloc(sizeof(struct sockaddr_in));

	char *tok= (char*) malloc(sizeof(char)*BUFSIZE);
	strcpy(tok, straddr.c_str());

	char *ip= strtok(tok, ":");
	char *port= strtok(NULL, ":");
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof hints);
	hints.ai_family= AF_INET;
	hints.ai_socktype= SOCK_DGRAM;
	int result= getaddrinfo(ip, port, &hints, &res);

	return res->ai_addr;
}

int sendToLast(struct text *msg, int msglen) {
	return sendMessage((struct sockaddr*)&lastUser, lastSize, msg, msglen);
}


std::string addrToString(const struct sockaddr_in* addr) {

	char *addrCstr= (char*) malloc(sizeof(char)*BUFSIZE);

	inet_ntop(AF_INET, &(addr->sin_addr), addrCstr, BUFSIZE);

	std::string ipStr= addrCstr;
	free(addrCstr);

	char port[6];
	snprintf(port, 6, "%hu", addr->sin_port);
	ipStr= (ipStr + ":" + port);

	return ipStr;
}

int addServerToChannel(std::string saddr, const char *channel) {

	std::string chanStr= channel;
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;

	// Check for channel dups in userToChan; add if none
	ii= map_chanToServer.equal_range(chanStr);
	unsigned int x= map_chanToServer.size();

	for (it= ii.first; it != ii.second; it++) {
		if (it->first == chanStr && it->second == saddr) {
		return false;
		}
	}
	map_chanToServer.insert(std::pair<std::string, std::string>(chanStr, saddr));
	return true;
}

int setupSocket(char *addr, char *port) {

	int result= 0;

	// Setup hints
	struct addrinfo hints;
	memset(&hints, 0, sizeof hints);
	hints.ai_family= AF_INET;
	hints.ai_socktype= SOCK_DGRAM;
	hints.ai_flags= AI_PASSIVE;

	// Get address info struct
	if ((result= getaddrinfo(addr, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
		return false;
	}

	// Create UDP socket
	sockfd= socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

	if (sockfd == -1) {
	perror("socket failed \n");
		return false;
	}

	//bind		
	if ((bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
		perror("bind failed \n");
		return false;
	}
	return true;
}

std::string addrToUser(const struct sockaddr_in* addr) {

	std::string addrStr= addrToString(addr);
	return map_addrToUser[addrStr];
}

int removeLastUser() {

	std::string addrStr= addrToString((struct sockaddr_in*)&lastUser);
	std::string userStr= map_addrToUser[addrStr];

	if (userStr == "") { // No user to remove
		printf("removeLastUser: Unknown source record to remove \n");
		return false;
	}

	map_addrToUser.erase(addrStr);
	map_userToAddr.erase(userStr);

	printf("%s %s recv Request Logout for user %s \n", myHostip.c_str(), addrStr.c_str(), userStr.c_str());

	return true;
}

int removeUserFromChannel(const char *username, const char *channel) {

	if (username == NULL) {
		printf("removeUserFromChannel: username was NULL\n");
		return false;
	} else if (channel == NULL || strlen(channel) > CHANNEL_MAX) {
		printf("removeUserFromChannel: channel was NULL\n");
		return false;
	}
	
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
	std::string userStr= username;
	std::string chanStr= channel;
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	bool seen= false;

	// Remove first matching channel from userToChan
	ii= map_userToChan.equal_range(userStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == chanStr) {
			seen= true;
			//printf("Removed channel %s from user %s\n", it->second.c_str(), it->first.c_str());
			map_userToChan.erase(it);
			break;
		}
	}
	if (!seen) {
		printf("%s %s did not find Channel %s recorded for User %s \n", myHostip.c_str(), lastAddr.c_str(), channel, username);
	
	}

	// Remove first matching user from chanToUser

	ii= map_chanToUser.equal_range(chanStr);
	seen= false;

	for (it= ii.first; it != ii.second; ++it) {
		if (it->second == userStr) {
			seen= true;
			map_chanToUser.erase(it);

			break;
		}
	}
	if (!seen) {
		printf("%s %s did not find User %s recorded for Channel %s \n", myHostip.c_str(), lastAddr.c_str(), username, channel);
	}

	return true;
}	


int recv_login(struct request_login *req) {

	addUser(req->req_username);
	return true;
}

int recv_logout(struct request_logout *req) {

	if (req == NULL) {return false;}

	int result= false;
	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;

	ii= map_userToChan.equal_range(userStr);

	char *userCstr= (char*) malloc(sizeof(char) * BUFSIZE);
	char *channel= (char*) malloc(sizeof(char) * BUFSIZE);
	strcpy(userCstr, userStr.c_str());

	for (it= ii.first; it != ii.second; ++it) {
		strcpy(channel, it->second.c_str());
		if (channel == NULL) return false;
		result= removeUserFromChannel(userCstr, channel);
	}
	result= removeLastUser();

	free(userCstr);
	free(channel);
	return result;
}


int addUser(const char* username) {

	if (username == NULL) {

		printf("username was NULL\n");
		return false;
	}

	std::string userStr = username;
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
	std::string storedAddr = map_userToAddr[userStr];


	if (storedAddr != "" && lastAddr != storedAddr) {
		printf("%s %s recv Request Login with username %s; replacing %s with %s\n",myHostip.c_str(), lastAddr.c_str(), username, storedAddr.c_str(), lastAddr.c_str());
		map_addrToUser.erase(storedAddr);
	} else {
		printf("%s %s recv Request Login with username %s\n",myHostip.c_str(), lastAddr.c_str(), username);
	}

	map_addrToUser[lastAddr]= userStr;
	map_userToAddr[userStr]= lastAddr;
	return true;
}

int recv_join(struct request_join *req) {

	std::string userStr = addrToUser((struct sockaddr_in*)&lastUser);
	std::string chan= req->req_channel;

	set_chanList.insert(chan);

	return addUserToChannel(userStr.c_str(), req->req_channel);
}

int recv_leave(struct request_leave *req) {

	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
	char *userCstr= (char*) malloc(sizeof(char) * BUFSIZE);

	strcpy(userCstr, userStr.c_str());

	printf("%s %s recv Request Leave for User %s from Channel %s \n", myHostip.c_str(), lastAddr.c_str(), userCstr, req->req_channel);

	int result= removeUserFromChannel(userCstr, req->req_channel);

	free(userCstr);
	return result;
}

int msg_s2s_say(struct request_s2s_say* req) {

	if (req == NULL) {
		return false;
	}

	//Insert uid into list
	set_sayList.insert(req->uid);

	int result= false;

	char *chan = (char*) malloc(sizeof(char) * BUFSIZE);
	sprintf(chan, "%s", req->req_channel);

	std::string chanStr= chan;
	struct sockaddr_in *sin= (sockaddr_in*) &lastUser;
	sin->sin_port= ntohs(sin->sin_port);
	std::string lastAddr= addrToString(sin);
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;

	// Check for channel dups in userToChan; add if none
	std::string getAddr = addrToString((struct sockaddr_in*)&lastUser);

	ii= map_chanToServer.equal_range(chanStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->first == chanStr && it->second != lastAddr) {
			struct sockaddr *sa= new_stringToAddr(it->second);
			printf("%s %s send S2S Say %s %s '%s' \n", myHostip.c_str(), getAddr.c_str(), req->req_username, req->req_channel, req->req_text);
			result= s2s_send(sa, sizeof(struct sockaddr_in), (struct request *)req, sizeof(struct request_s2s_say)) || result;
		}
	}
	return result;
}

int msg_s2s_say(char *username, char *channel, char *msg) {

	if (username == NULL || channel == NULL || msg == NULL) {
		return false;
	}

	struct request_s2s_say *req= (struct request_s2s_say*) malloc(sizeof(struct request_s2s_say));

	req->req_type= ((int)REQ_S2S_SAY);
	

	strncpy(req->req_channel, channel, CHANNEL_MAX);
	strncpy(req->req_username, username, USERNAME_MAX);
	strncpy(req->req_text, msg, SAY_MAX);

	//give message unique ID
	req->uid= genuid();

	//printf("%d\n", req->uid);

	int result= msg_s2s_say(req);
	free(req);

	return result;
}

int msg_list() {

	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);

	std::set<std::string> channels;
	std::multimap<std::string,std::string>::iterator it;
	std::set<std::string,std::string>::iterator sit;

	for (it= map_chanToUser.begin(); it != map_chanToUser.end(); it++) {
		channels.insert(it->first);
	}

	if (channels.size() == 0) { 
		printf("No channels exist on this server \n");
		return false;
	}

	struct text_list *txt= (struct text_list*) malloc(sizeof(struct text_list) + channels.size()*sizeof(struct channel_info));

	txt->txt_type= ((int)TXT_LIST);
	txt->txt_nchannels= ((int)channels.size());

	int i= 0;

	for (sit= channels.begin(); sit != channels.end(); sit++) {
		strncpy(txt->txt_channels[i++].ch_channel, sit->c_str(), CHANNEL_MAX);
	}

	int result= sendToLast((struct text*)txt, sizeof(struct text_list) + channels.size()*sizeof(struct channel_info));

	if (result == true) {
		printf("%s %s recv Request List \n" ,myHostip.c_str(), lastAddr.c_str());
	}

	free(txt);
	return true;
}

int msg_who(const char *channel) {

	if (channel == NULL) {
		printf("msg_who: channel was NULL \n");
		return false;
	}

	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	std::string chanStr= channel;

	int numUsers= 0;
	ii= map_chanToUser.equal_range(chanStr);

	for (it= ii.first; it != ii.second; it++) {
		++numUsers;
	}

	if (numUsers == 0) {
		printf("%s %s recv Request Who, Channel %s does not exist \n" ,myHostip.c_str(), lastAddr.c_str(), channel);
		return false;
	}

	struct text_who *txt= (struct text_who*) malloc(sizeof(struct text_who) + numUsers*sizeof(struct user_info));

	txt->txt_type= ((int)TXT_WHO);
	txt->txt_nusernames= ((int)numUsers);

	strncpy(txt->txt_channel, channel, CHANNEL_MAX);

	int i= 0;
	ii= map_chanToUser.equal_range(chanStr);

	for (it=ii.first; it != ii.second; it++) {
		strncpy(txt->txt_users[i++].us_username, it->second.c_str(), USERNAME_MAX);
	}

	int result= sendToLast((struct text*)txt, sizeof(struct text_who) + numUsers*sizeof(struct user_info));
	if (result == true) {
		printf("%s %s recv Request Who \n" ,myHostip.c_str(), lastAddr.c_str());
	}

	free(txt);

	return result;
}


int recv_list(struct request_list *req) {

	if (req == NULL) { 
		return false; 
	}

	return msg_list();
}

int recv_say(struct request_say *req, long long uid, char *uname) {

	int result= false;

	if (uname == NULL) {
		std::string fromUser= addrToUser((struct sockaddr_in*)&lastUser);
		uname = (char*) malloc(sizeof(char) * BUFSIZE);
		strncpy(uname, fromUser.c_str(), USERNAME_MAX);	
	}


	// for every channel the user is a member of
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> cii;
	std::multimap<std::string,std::string>::iterator cit;
	std::string req_channel= req->req_channel;
	cii= map_chanToUser.equal_range(req_channel);

	for (cit= cii.first; cit != cii.second; cit++) {

		std::string userStr= cit->second;
		std::string userAddr= map_userToAddr[userStr];

		char *tok= (char*) malloc(sizeof(char)*BUFSIZE);
		strcpy(tok, userAddr.c_str());

		char *ip= strtok(tok, ":");
		char *port= strtok(NULL, ":");
		u_short p= (u_short) atoi(port);

		struct sockaddr_in sa;
		inet_pton(AF_INET, ip, &(sa.sin_addr));
		sa.sin_port= p;
		sa.sin_family= AF_INET;

		std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
		std::string fromUser= addrToUser((struct sockaddr_in*)&lastUser);

		printf("%s %s recv Request Say %s '%s'\n", myHostip.c_str(), lastAddr.c_str(), req->req_channel, req->req_text);

		msg_say(req->req_channel, uname, req->req_text, (struct sockaddr*)&sa);

		result= true;
		free(tok);
	}
	
	//if uid is 0 we know it is fresh
	if (uid == 0) {

		//serpate function call for new request
		result = msg_s2s_say(uname, req->req_channel, req->req_text) || result;

	} else { //has been given ID will check inside msg_s2s_say
		
		struct request_s2s_say* sreq= (struct request_s2s_say*) malloc(sizeof(struct request_s2s_say));

		strncpy(sreq->req_username, uname, USERNAME_MAX);
		strncpy(sreq->req_channel, req->req_channel, CHANNEL_MAX);
		strncpy(sreq->req_text, req->req_text, SAY_MAX);

		sreq->uid= uid;
		sreq->req_type = ((int)REQ_S2S_SAY);
			
		result = msg_s2s_say(sreq) || result;
		free(sreq);
	}

	return result; 
}
	
int addUserToChannel(const char *username, const char *channel) {
	if (username == NULL ) {
		printf("addUserToChannel: username was NULL \n");
		return false;
	} else if (channel == NULL) {
		printf("addUserToChannel: channel was NULL \n");
		return false;
	}
	std::string userStr= username;
	std::string chanStr= channel;
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;
	bool seen= false;

	// Check for channel dups in userToChan; add if none
	ii= map_userToChan.equal_range(userStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == chanStr) {
			seen= true;
			printf("%s %s recv Request Join %s for User %s; User already belongs to channel \n",myHostip.c_str(), lastAddr.c_str(), channel, username);
			return false;
		}
	}
	if (!seen) {
		map_userToChan.insert( std::pair<std::string,std::string>(userStr, chanStr) );
		printf("%s %s recv Request Join %s for User %s \n",myHostip.c_str(), lastAddr.c_str(), channel, username);
		msg_s2s_join(channel);
	}

	// Check for username dups in chanToUser; add if none
	seen= false;
	ii= map_chanToUser.equal_range(chanStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->second == userStr) {
			seen = true;
			break;
		}
	}
	if (!seen) {
		map_chanToUser.insert(std::pair<std::string,std::string>(chanStr, userStr));
	}

	return true;
}


int s2s_send(const struct sockaddr *addr, size_t addrlen, struct request *msg, int msglen) {

	int result= sendto(sockfd, msg, msglen, 0, addr, addrlen);
	if (result == -1) {
		perror("send failed \n");
		return false;
	}
	return true;
}

int s2s_forward(struct request *msg, int msglen, int type, std::string channel) {

	struct sockaddr_in* saddr= (struct sockaddr_in*) &lastUser;
	saddr->sin_port= ntohs(saddr->sin_port);
	std::string lastAddr= addrToString((struct sockaddr_in*) &lastUser);

	unsigned int i, result= false;

	if (vec_serverAddrs.size() == 0) {return false;}

	for (i= 0; i < vec_serverAddrs.size(); i++) {

		//send to proper addresses
		if (lastAddr != vec_serverAddrs[i]) {
			result= true;
			const struct sockaddr * sa= new_stringToAddr(vec_serverAddrs[i]);
			result= s2s_send(sa, sizeof(struct sockaddr_in), msg, msglen) && result;
			printf("%s %s send S2S Join %s \n", myHostip.c_str(), vec_serverAddrs[i].c_str(), channel.c_str());
		}
	}
	return result;
}


int recv_who(struct request_who *req) {

	return msg_who(req->req_channel);
}

int msg_s2s_join(const char *channel) {

	std::string addrStr= addrToString((struct sockaddr_in*)&lastUser);

	struct request_s2s_join *req= (struct request_s2s_join*) malloc(sizeof(struct request_s2s_join));

	req->req_type= ((int)REQ_S2S_JOIN);
	strncpy(req->req_channel, channel, CHANNEL_MAX);

	int result = s2s_forward((struct request*)req, sizeof(struct request_s2s_join), REQ_S2S_JOIN,channel);
	
	unsigned int i;

	for (i= 0; i < vec_serverAddrs.size(); i++) {
		addServerToChannel(vec_serverAddrs[i], channel);	
	}

	free(req);
	return result;
}

int removeServerFromChannel(std::string saddr, char *channel) {

	std::string chanStr= channel;
	std::pair<std::multimap<std::string,std::string>::iterator,std::multimap<std::string,std::string>::iterator> ii;
	std::multimap<std::string,std::string>::iterator it;

	ii= map_chanToServer.equal_range(chanStr);
	for (it= ii.first; it != ii.second; it++) {
		if (it->first == chanStr && it->second == saddr) {
			map_chanToServer.erase(it);
			return true;
		}
	}
	return false;
}

int recv_s2s_leave(struct request_s2s_leave *req) {

	if (req == NULL) { 
		return false; 
	}

	int result= true;
		
	struct sockaddr_in* sin= (struct sockaddr_in*) &lastUser;
	sin->sin_port= ntohs(sin->sin_port);

	std::string saddr= addrToString(sin);
	std::string userStr= addrToUser((struct sockaddr_in*)&lastUser);

	printf("%s %s recv S2S leave Channel %s\n", myHostip.c_str(), saddr.c_str(), req->req_channel);

	result= removeServerFromChannel(saddr, req->req_channel);
	
	return result;
}

int recv_s2s_join(struct request_s2s_join *req) {

	std::string chan= req->req_channel;

	struct sockaddr_in *sa= (struct sockaddr_in*) &lastUser;
	sa->sin_port= ntohs(sa->sin_port);
	

	std::string saddr= addrToString((struct sockaddr_in*) &lastUser);
	sa->sin_port= htons(sa->sin_port);

	addServerToChannel(saddr, req->req_channel);

	printf("%s %s recv S2S Join %s \n", myHostip.c_str(), saddr.c_str(), chan.c_str());
	
	
	if (set_chanList.find(chan) == set_chanList.end()) {
		set_chanList.insert(chan);
		return msg_s2s_join(req->req_channel);
	}
	return false;
}


int s2s_sendToLast(struct request *msg, int msglen, int type) {

	struct sockaddr_in *saddr= (struct sockaddr_in*)&lastUser;

	std::string addr= addrToString((struct sockaddr_in*)&lastUser);
	saddr->sin_port= htons(saddr->sin_port);

	return s2s_send((struct sockaddr*)&lastUser, lastSize, msg, msglen);
}

int msg_s2s_leave(const char *channel) {

	if (channel == NULL) {
		return false;
	}

	struct request_s2s_leave *req= (struct request_s2s_leave*) malloc(sizeof(struct request_s2s_leave));
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);

	req->req_type= ((int)REQ_S2S_LEAVE);

	strncpy(req->req_channel, channel, CHANNEL_MAX);
	
	printf("%s %s send S2S leave \n", myHostip.c_str(), lastAddr.c_str());

	int result= s2s_sendToLast((struct request*) req, sizeof(struct request_s2s_leave), REQ_S2S_LEAVE);

	free(req);
	return result;
}

int recv_s2s_say(struct request_s2s_say *req) {

	struct request_say* sreq= (struct request_say*) malloc(sizeof(struct request_say));
	std::string lastAddr = addrToString((struct sockaddr_in*)&lastUser);

	strncpy(sreq->req_channel, req->req_channel, CHANNEL_MAX);
	strncpy(sreq->req_text, req->req_text, SAY_MAX);

	printf("%s %s recv S2S Say %s %s '%s' \n", myHostip.c_str(), lastAddr.c_str(), req->req_username, req->req_channel, req->req_text);
	
	// UID NOT FOUND IN LIST
	if(set_sayList.find(req->uid) == set_sayList.end()) {
	
		//printf("UID not found in list\n");
		int result= recv_say(sreq, req->uid, req->req_username);

		if (!result) { //could not forward message send leave 
			printf("%s %s Could not forward message sending leave\n", myHostip.c_str(), lastAddr.c_str());
			msg_s2s_leave(req->req_channel);
		}
		free(sreq);
		return result;
	} 


	//Duplicate  UID found
	printf("%s %s Duplicate UID deteced sending leave\n", myHostip.c_str(), lastAddr.c_str());
	struct sockaddr_in *sin= (struct sockaddr_in*) &lastUser;
	sin->sin_port= ntohs(sin->sin_port);
	msg_s2s_leave(req->req_channel);
	return false;
}

int recv_keepAlive(struct request_keep_alive *req) {

	if (req == NULL) { 
		return false; 
	}

	return true;
}

int switchRequest(struct request* req, int len) {

	int result = false;

	if (req->req_type != REQ_S2S_JOIN && req->req_type != REQ_S2S_LEAVE && req->req_type != REQ_S2S_SAY) {
		if (addrToUser((struct sockaddr_in*)&lastUser) == "" && req->req_type != REQ_LOGIN) {
			printf("Received request type %d from unknown user; ignoring\n", req->req_type);

			return result;
		}
	}

	//used for debugging request types
	//printf("request type: %d\n", req->req_type);

	switch((int)req->req_type) {

		case REQ_LOGIN:
			if (sizeof(struct request_login) != len) {
				printf("Wrong Size Login \n");
				result = false;
				break;
			}
			result= recv_login((struct request_login*)req);
			break;

		case REQ_LOGOUT:
			if (sizeof(struct request_logout) != len) {
				printf("Wrong Size Logout \n");
				result = false;
				break;
			}
			result= recv_logout((struct request_logout*)req);
			break;

		case REQ_JOIN:
			if (sizeof(struct request_join) != len) {
				printf("Received join Bad Length \n");
				result= false;
				break;
			}
			result= recv_join((struct request_join*)req );
			break;

		case REQ_LEAVE:
			if (sizeof(struct request_leave) != len) {
				printf("Leave Request Size\n");
				result= false;
				break;
			}
			result = recv_leave((struct request_leave*)req );
			break;

		case REQ_SAY:
			if (sizeof(struct request_say) != len) {
				printf("Say Request Size \n");
				result= false;
				break;
			}
			result= recv_say((struct request_say*) req, 0, NULL);
			break;

		case REQ_LIST:
			if (sizeof(struct request_list) != len) {
				printf("List Request Size\n");
				result= false;
				break;
			}
			result= recv_list((struct request_list*)req);
			break;

		case REQ_WHO:
			if (sizeof(struct request_who) != len) {
				printf("Who Request Size \n");
				result= false;
				break;
			}
			result= recv_who((struct request_who*)req);
			break;

		case REQ_KEEP_ALIVE:
			if (sizeof(struct request_keep_alive) != len) {
				printf("Keep_Alive Request Size \n");
				result= false;
				break;
			}
			result= recv_keepAlive((struct request_keep_alive*)req);
			break;

		case REQ_S2S_JOIN:
			if (sizeof(struct request_s2s_join) != len) {
				printf("S2S_Join size \n ");
	
				result= false;
				break;
			}
			result = recv_s2s_join((struct request_s2s_join*)req);
			break;

		case REQ_S2S_LEAVE:
			if (sizeof(struct request_s2s_leave) != len) {
				printf("S2S_Leave size\n");
				result= false;
				break;
			}
			result= recv_s2s_leave((struct request_s2s_leave*) req);
			break;

		case REQ_S2S_SAY:
			if (sizeof(struct request_s2s_say) != len) {
				printf("S2S_Say size\n");
				result= false;
				break;
			}
			result= recv_s2s_say((struct request_s2s_say*)req);
			break;
		default:
			printf("Unknown packet received \n");
			break;
	}

	return result;
}

