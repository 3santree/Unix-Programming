#include "reader.h"

bool c_FLAG;
bool d_FLAG;
bool h_FLAG;
bool i_FLAG;
bool l_FLAG;
bool p_FLAG;
 
char *cgidir;
char *docroot;
char *hostname;
char *port;
char *real_cgidir;
char *real_docroot;
int logFD;

void 
handle_socket(int server_fd) {
    /* Buffer for storing client request */
    int client_fd, childpid;
    for (;;) {
        // struct sockaddr_in cliAddr;
        // socklen_t cliAddr_size;
        
        /* Recieve client request */
        // int client_fd = accept(server_fd, (struct sockaddr*)&cliAddr, &cliAddr_size);
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            continue;
        }
        //printf("Connection accepted from %s:%d\n\n", inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port));
        //printf("Connection accepted");
        
        /* Fork one process for one client request */
        if ((childpid = fork()) == 0) {
            close(server_fd);
            char *s;
            // Reader
            s = reader(client_fd);
            printf("[reader return]%s\n\n", s);

            // Writer: Return a Hello world just for showcase
            writer(s, client_fd);
            /* Close the client socket connection */
            close(client_fd);
            exit(1);
        } else if (childpid < 0) {
            perror("fork()");
        } else {
            wait(NULL);
        }
    }
}

bool 
checkProtocol(char* protocol) {
    return ((strncmp(protocol, "HTTP/1.0", 9) == 0) ||
        (strncmp(protocol, "HTTP/0.9", 9) == 0));
}

bool 
checkMethod(char* method) {
    return ((strncmp(method, "GET", 4) == 0) ||
        (strncmp(method, "HEAD", 5) == 0));
}

bool
isPrefix(char* string, char* prefix) {
    return strncmp(string, prefix, strlen(prefix)) == 0;
}

#define FIELD_SIZE 64
typedef struct header_info {
    char header[FIELD_SIZE];
    char field[FIELD_SIZE];
    struct header_info *next;
} header_info;

header_info head;

void
printHeader() {
    header_info *ptr;
    for (ptr = head.next; ptr != NULL; ptr = ptr->next) {
        printf("%s: %s\n", ptr->header, ptr->field);
    }
}

static const char *rfc1123_date = "%a, %d %B %Y %T %Z";
static const char *rfc850_date = "%a, %d-%B-%y %T %Z";
static const char *ansic_date = "%a %B %d %T %Y";

int
isValidDate(char *date)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (strptime(date, rfc1123_date, &tm) != NULL)
        return 0;
    if (strptime(date, rfc850_date, &tm) != NULL)
        return 0;
    if (strptime(date, ansic_date, &tm) != NULL)
        return 0;
    return 1;
}
 
#define N_DATE_HEADS 2
char valid_headers[][FIELD_SIZE] = {
    "Date",
    "Last-Modified",
    "Server",
    "Content-Type",
    "Content-Length"
};

header_info*
getHeaderContent(char *line, header_info *ptr) {
    header_info *next;
    char header[FIELD_SIZE], field[FIELD_SIZE];
    size_t i;

    if (sscanf(line," %[^: ] : %[^\t]", header, field) < 2) {
        return ptr;
    }

    for (i = 0; i < sizeof(valid_headers) / FIELD_SIZE; i++) {
        if (strcmp(header, valid_headers[i]) == 0) {
            break;
        }
    }

    if (i == sizeof(valid_headers) / FIELD_SIZE) {
        return ptr;
    }

    if (i < N_DATE_HEADS && isValidDate(field) != 0) {
        fprintf(stderr, "Invalid time format\n");
        return ptr;
    }

    if ((next = malloc(sizeof(header_info))) == NULL) {
        perror("malloc()");
    }

    strlcpy(next->header, header, FIELD_SIZE);
    strlcpy(next->field, field, FIELD_SIZE);

    ptr->next = next;
    return next;
}

void
updatePath(char** updated_path, char* path, char* initial) {
    char* part;
    int size;
    part = strtok(strdup(path), "/");
    size = strlcat((char *)*updated_path, initial, PATH_MAX - strlen((const char *)*updated_path));
    ((char *)*updated_path)[size + 1] = '\0';

    while (part != NULL) {
        if (strncmp(part, "..", 3) == 0) {
            char* update, *previous;
            char* tmp = strdup((char *)*updated_path);
            while ((update = strstr(strdup(tmp), "/")) != NULL) {
                tmp = update + 1;
                previous = strdup(tmp);
            }
            if ((update = strstr((char *)*updated_path, previous)) != NULL) {
                int current_index = 0;
                current_index = update - (char *)*updated_path;
                ((char *)*updated_path)[current_index - 1] = '\0';
            }
        } else {
            size = strlcat((char *)*updated_path, "/", PATH_MAX - strlen((const char *)*updated_path));
            ((char *)*updated_path)[size + 1] = '\0';
            size = strlcat((char *)*updated_path, part, PATH_MAX - strlen((const char *)*updated_path));
            ((char *)*updated_path)[size + 1] = '\0';
        }
        part = strtok(NULL, "/");
    }
}

char *
checkPath(char* path) {
    char* newpath;
    char* host;
    char* part;
    char* prefix_root;
    char* updated_path;
    int size;

    prefix_root = strdup(real_docroot);

    if (path[0] != '/') {
        /* Remove schema and hostname from path. */
        if ((host = strstr(strdup(path), "://")) != NULL) {
            host = (host+3);
        } else {
            host = strdup(path);
        }
        newpath = strstr(strdup(host),"/");
    } else {
        newpath = strdup(path);
    }

    if ((updated_path = (char *)malloc(sizeof(char)*(PATH_MAX+1))) == NULL) {
        if (d_FLAG) {
            (void)printf("malloc: %s\n", strerror(errno));
        }
        exit(1);
    } 
    updated_path[0] = '\0';

    if (isPrefix(newpath, "/cgi-bin/") && c_FLAG) {
        /* 
         * /cgi-bin/ can be a valid directory within the docroot. 
         * Only convert /cgi-bin/ into the path to cgidir if the c flag is set.
         */
        prefix_root = strdup(real_cgidir);
        updatePath(&updated_path, newpath+8, real_cgidir);
    } else if ((!isPrefix(newpath, "/~/") && isPrefix(newpath, "/~"))) {
    
        /* Resolve ~user to home directory of that user. */
        char *homedir;
        struct passwd *user;

        if ((homedir = (char *)malloc(sizeof(char)*(PATH_MAX+1))) == NULL) {
            if (d_FLAG) {
                (void)printf("malloc: %s\n", strerror(errno));
            }
            exit(1);
        } 

        part = strtok(strdup(newpath), "/");
        if ((user = getpwnam(part+1)) == NULL) {
            /* error here */
            fprintf(stderr,"err: %s\n", strerror(errno));
            exit(1);
        }
    
        size = strlcat(homedir, user->pw_dir, PATH_MAX - strlen(homedir));
        homedir[size + 1] = '\0';
        size = strlcat(homedir, "/sws", PATH_MAX - strlen(homedir));
        homedir[size + 1] = '\0';
        prefix_root = strdup(homedir);
        updatePath(&updated_path, newpath+(strlen(part)+1), homedir);
    } else {
        /* No special case, prepend real_docroot. */
        updatePath(&updated_path, newpath, real_docroot);
    }

    if(!isPrefix(updated_path, prefix_root)) {
        /* Do until prefix match and then add all remaining */
        char* part_docroot;
        char* part_updated_path;
        char *p1, *p2;

        part_docroot = strtok_r(strdup(prefix_root), "/", &p1);
        part_updated_path = strtok_r(strdup(updated_path),"/", &p2);
        
        while (part_docroot != NULL && part_updated_path != NULL) {
            if(
                (strlen(part_docroot) != strlen(part_updated_path)) || 
                (strncmp(part_docroot, part_updated_path, strlen(part_docroot)) != 0)
            ){
                char* final_updated_path;
                if ((final_updated_path = (char *)malloc(sizeof(char)*(PATH_MAX+1))) == NULL) {
                    exit(1);
                } 
                final_updated_path[0] = '\0';
                size = strlcat(final_updated_path, prefix_root, PATH_MAX - strlen(final_updated_path));
                final_updated_path[size + 1] = '\0';
                size = strlcat(final_updated_path, "/", PATH_MAX - strlen(final_updated_path));
                final_updated_path[size + 1] = '\0';
                size = strlcat(final_updated_path, part_updated_path, PATH_MAX - strlen(final_updated_path));
                final_updated_path[size + 1] = '\0';
                updated_path = strdup(final_updated_path);
                break;
            }
            part_docroot = strtok_r(NULL, "/", &p1);
            part_updated_path = strtok_r(NULL, "/", &p2);
        }
    }

    char* real_updated;
    if ((real_updated = (char *)malloc(sizeof(char)*PATH_MAX)) == NULL) {
        (void)fprintf(stderr, "malloc: %s\n", strerror(errno));
        exit(1);
    } else if (realpath(updated_path, real_updated) == NULL) {
        (void)fprintf(stderr, "realpath of %s: %s\n", updated_path, strerror(errno));
        exit(1);
    } else {
        if(!isPrefix(real_updated, prefix_root)) {
            real_updated = strdup(prefix_root);
        }
        if (d_FLAG) {
            (void)printf("The realpath of %s is %s\n", updated_path, real_updated);
        }
    }
    return real_updated;
}

void
logging(char* remoteAddress, char* reqestedTime, char* firstLineOfRequest, char* status, char* responseSize) {
    char* logging_buffer;
    int n;

    if ((logging_buffer = (char *)malloc(sizeof(char)*BUFSIZE)) == NULL) {
        if (d_FLAG) {
            (void)printf("malloc: %s\n", strerror(errno));
        }
		exit(1);
    } 
    sprintf(logging_buffer, "%s %s %s %s %s", remoteAddress, reqestedTime, firstLineOfRequest, status, responseSize);
    if((n = write(logFD, logging_buffer, sizeof(logging_buffer))) == -1){
        if (d_FLAG) {
            (void)printf("Error while logging into file: %s\n", strerror(errno));
        }
		exit(1);
    }
}

char *
reader(int fd) {
    char buf[BUFSIZE];
    char* method;
    char* part;
    char* path;
    char* protocol;
    int index;

    header_info *ptr = &head;

    recv(fd, buf, BUFSIZE, 0);
    int n = 0;
    char* lines[1024] = {NULL};
    char* line = NULL;
    line = strtok(buf, "\r\n");
    /* Read every line into *lines[1024] */
    while (line != NULL) {
        lines[n] = strdup(line);
        n++;
        line = strtok(NULL, "\r\n");
    }
    n = 0;
    while (lines[n] != NULL) {
        line = lines[n];
        printf("[%d]%s\n",n+1, line);
        if (n == 0) {
            /* First line */
            printf("[First Line]\n");
            // char *method, *part, *protocol
            part = strtok(line, " ");
            index = 0;

            while (part != NULL) {
                if (index == 0) {
                    method = strdup(part);
                } else if (index == 1) {
                    path = strdup(part);
                } else if (index == 2) {
                    protocol = strdup(part);
                }
                index++;
                printf("\t[%d]%s\n",index, part);
                part = strtok(NULL, " ");
            }
            // First line should be 3 fields, or it's invalid
            // method path protocol
            if (index != 3) {
                /* Error here */
            } else {
                if (!checkMethod(method)) {
                    /* Method was not "GET" or "HEAD" */
                    if (d_FLAG) {
                        (void)printf("405 Method Not Allowed");
                    }
                    printf("Mehotd Error\n");
                    return "405 Method Not Allowed";
                } else if (!checkProtocol(protocol)){
                    printf("Protocol Error\n");
                    return "CHANGE";
                }
                /* Yu: Everything went okay if I comment this line */
                path = checkPath(path);
                /* Actually serve. */

                
                /* Logging if l flag is given 
                if(l_FLAG) {
                    logging(method, path, protocol, response);
                }
                */
                
            }
        } else {
            /* (Header) Anything other than First line */
            ptr = getHeaderContent(line, ptr);
        }
        line = strtok(NULL, "\r\n");
        n++;
    }
    printHeader();
    return "CHANGE";
}
