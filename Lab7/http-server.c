/*
 * tcp-recver.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

static void die(const char *s) {
    perror(s);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <server-port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n", argv[0]);
        exit(1);
    }

    unsigned short port = atoi(argv[1]);
    char *web_root = argv[2];
    char *mdbhost = argv[3];
    unsigned short mdbport = atoi(argv[4]);
    int mdbsock;

    //HTML Formatting
    char bodystart[] = "<html><body><h1>";
    char bodyend[] = "</h1></body></html>";

    //MDB Formatting
    const char *form =
	"<h1>mdb-lookup</h1>\n"
	"<p>\n"
	"<form method=GET action=/mdb-lookup>\n"
	"lookup: <input type=text name=key>\n"
	"<input type=submit>\n"
	"</form>\n"
	"<p>\n";

    //CREATING MDB CONNECTION
    struct hostent *he;
    if ((he = gethostbyname(mdbhost)) == NULL) {
        die("gethostbyname failed");
    }

    char *mdbIP = inet_ntoa(*(struct in_addr *) he->h_addr);
 
    if((mdbsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        die("socket failed");
    } 

    //CONSTRUCT MDB SERVER
    struct sockaddr_in mdbservaddr;
    memset(&mdbservaddr, 0, sizeof(mdbservaddr)); // must zero out the structure
    mdbservaddr.sin_family = AF_INET;
    mdbservaddr.sin_addr.s_addr = inet_addr(mdbIP);
    mdbservaddr.sin_port = htons(mdbport); // must be in network byte order

    //CONNECT TO MDB SERVER
    if(connect(mdbsock,(struct sockaddr *) &mdbservaddr, sizeof(mdbservaddr)) < 0) {
        die("connect failed"); 
    }

    // Create a listening socket (also called server socket)
    int servsock;
    if ((servsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    // Construct local address structure
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // any network interface
    servaddr.sin_port = htons(port);

    // Bind to the local address
    if (bind(servsock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
        die("bind failed");

    // Start listening for incoming connections
    if (listen(servsock, 5 /* queue size for connection requests */ ) < 0)
        die("listen failed");

    int clntsock;
    socklen_t clntlen;
    struct sockaddr_in clntaddr;

    //PID
    pid_t pid;

    while (1) {
        clntlen = sizeof(clntaddr); // initialize the in-out parameter
        if ((clntsock = accept(servsock, (struct sockaddr *) &clntaddr, &clntlen)) < 0) {
            die("accept failed");
	}

	//FORK CHILD PROCESS LOOP
	pid = fork();

	if(pid != 0) {
	    close(clntsock);
	}

	if(pid == 0) { // CHILD PROCESS
	    char firstline[1000];
	    FILE *clntInput;
	    FILE *fp;
	    char buf[4096];
	    char dir[256];
	    int error_code = 200;
	    int mdbflag = 0;
	    char error_msg[128];
	    char *clntip = inet_ntoa(clntaddr.sin_addr);
	    char *isfile;
	    char *mdbkey;

	    sprintf(error_msg, "200 OK");
	    clntInput = fdopen(clntsock, "r");

	    // Parse Header Request
	    fgets(firstline, sizeof(firstline) - 1, clntInput);
	    for(int i=0; i < strlen(firstline); i++){
		if(firstline[i] == '\r' || firstline[i] == '\n') {
		    firstline[i] = '\0';
		}
	    }

	    //Begins with GET
	    if(strncmp(firstline, "GET ", 4) == 0) {
		if(firstline[4] == '/') {
    		    char *pos = strchr(firstline, '/');
		    strcpy(dir, pos);
    		    for(int i = 0; i < strlen(dir); i++) {
    			if((dir[i] == ' ') || dir[i] == '\r') {
			    dir[i] = '\0';
    			}
    		    }		
    		} else {
		    error_code = 400;
		    sprintf(error_msg, "400 Bad Request");
    		}
	    } else {
		error_code = 501;
		sprintf(error_msg, "501 Not Implemented");
	    }
          
	    //TO CHECK IF FILE
	    isfile = strrchr(dir, '/');

	    //Contains /../
	    if((strstr(dir, "/../") != NULL)) {
	       	error_code = 403;
		sprintf(error_msg, "403 Forbidden");
	    }

	    //Contains /..
	    if(strlen(dir) >= 3 && strncmp(dir+strlen(dir)-3, "/..", 3) == 0) {
		error_code = 403;
		sprintf(error_msg, "403 Forbidden");
	    }

	    //CHECKING FOR MDB-LOOKUP IN URL
	    if(strcmp(dir, "/mdb-lookup") == 0) { 		
		error_code = 200;
		mdbflag = 1;		
	    }

	    if(strncmp(dir, "/mdb-lookup?key=", 16) == 0) {
		error_code = 200;
		mdbflag = 2;
	    }

            //If dir ends with / append Index.html	
	    if(mdbflag == 0 && error_code == 200) {
		if((dir[strlen(dir)-1]) == '/' && (strchr(isfile, '.') == NULL)) {
		    strcat(dir, "index.html");
		} else {		
		    if((dir[strlen(dir)-1]) != '/' && (strchr(isfile, '.') == NULL)) {
			strcat(dir, "/index.html");
		    }	    
		}
	    }

	    //CREATE A DIR PATH IF NOT MDB-LOOKUP
	    if(mdbflag == 0 && error_code == 200) {
		char path[512];
		sprintf(path, "%s%s", web_root, dir);
	       	if((fp = fopen(path, "rb+")) == NULL) {
		    error_code = 404;
		    sprintf(error_msg, "404 Not Found");
		}
	    }

	    //Construct Response Header
	    char response_header[4096];

	    if(mdbflag == 0) {
	       	if(error_code == 200) {
		    sprintf(response_header, "HTTP/1.0 %s\r\n\r\n", error_msg);
	       	} else {
	    	    sprintf(response_header, "HTTP/1.0 %s\r\n\r\n%s %s %s", error_msg, bodystart, error_msg, bodyend);
	       	}
		printf("%s \"%s\" %s\n", clntip, firstline, error_msg);
		send(clntsock, response_header, strlen(response_header), 0);
	    }

	    if(mdbflag == 1) {
		sprintf(response_header, "HTTP/1.0 %s\r\n\r\n<html><body>%s</body></html>", error_msg, form);
		printf("%s \"%s\" %s\n", clntip, firstline, error_msg);
		send(clntsock, response_header, strlen(response_header), 0);
	    }

	    if(mdbflag == 2) {
		char mdbkeycopy[4096];
		char bufmdb[4096];
		FILE *mdbinput = fdopen(mdbsock, "r+b");
    		int counter = 0;

		mdbkey = strchr(dir, '=');
		mdbkey++;
		strcpy(mdbkeycopy, mdbkey);
		strcat(mdbkeycopy, "\n");
		sprintf(response_header, "HTTP/1.0 %s\r\n\r\n<html><body>%s", error_msg, form);
		printf("looking up [%s]: %s \"%s\" %s\n", mdbkey, clntip, firstline, error_msg);
		send(clntsock, response_header, strlen(response_header), 0);
		sprintf(response_header, "<table border>\n");
		send(clntsock, response_header, strlen(response_header), 0);
		send(mdbsock, mdbkeycopy, strlen(mdbkeycopy), 0);
	
		while(1) {
		    char mdbresults[4096];
		    fgets(bufmdb, sizeof(bufmdb), mdbinput);
		    if(bufmdb[0] == '\n' || (bufmdb[0] == '\r' && bufmdb[1] == '\n')) {
			break;
		    }
		
		    if(counter%2 == 0) {
                    	sprintf(mdbresults, "<tr> <td> %s", bufmdb);
		    } else {
			sprintf(mdbresults, "<tr> <td bgcolor=yellow> %s", bufmdb);
		    }
		    send(clntsock, mdbresults, strlen(mdbresults), 0);
		    counter++;
		}
		sprintf(response_header, "</table>\n</body></html>\n");
		send(clntsock, response_header, strlen(response_header), 0);
	    }
      
      	    //READ AND SEND FILE
	    if(mdbflag == 0 && error_code == 200) {
    		size_t nread;
    		while((nread = fread(buf, 1, sizeof(buf), fp)) != 0) {
    		    send(clntsock, buf, nread, 0);
    		}
	    }			    

	    //Close Socket and Exit
	    close(clntsock);
	    exit(1);
	}
    }
}
