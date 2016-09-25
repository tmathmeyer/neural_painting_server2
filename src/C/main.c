#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "cref/types.h"
#include "cref/ref.h"
#include "cttp/http.h"
#include "cttp/http_syntax_macros.h"

typedef
struct exec_queue {
    char *path;
    struct exec_queue *next;
} exec_queue;


exec_queue *head = NULL;
exec_queue *tail = NULL;
pthread_mutex_t queue_lock;

size_t get_queue_status(char *c) {
    size_t result = 0;
    pthread_mutex_lock(&queue_lock);
    printf("LOCKED AT: %i\n", __LINE__);
    exec_queue *nh = head;
    while(nh) {
        if (!strncmp(nh->path, c, 6)) {
            goto ret;
        }
        nh = nh->next;
        result++;
    }
    result = SIZE_MAX;
ret:
    printf("UNLOCKED AT: %i\n", __LINE__);
    pthread_mutex_unlock(&queue_lock);
    return result;
}

//65 - 90
//48 - 57
char *genuuid_str(void) {
    size_t len = 6;
    char *res = calloc(sizeof(char), len);
    for(int i=0; i<len; i++) {
        int n = rand()%36;
        if (n < 10) {
            res[i] = '0'+n;
        } else {
            res[i] = 'A'+n-10;
        }
    }
    return res;
}

char *dirappend(char *a, char *b) {
    char *res = calloc(sizeof(char), strlen(a) + strlen(b) + 2);
    strcat(res, a);
    strcat(res, "/");
    strcat(res, b);
    return res;
}


HTTP(recieveimages) {
    char *uuid = genuuid_str();
    char *proc_uuid = dirappend("processing", uuid);
    if (mkdir(proc_uuid, S_IRWXU)) {
        HTTP_STATUS(500, "ERROR", "text/json");
        HTTP_WRITE("{'cause': 'could not create directory for images',\n'bad_data':'");
        HTTP_WRITE(uuid);
        HTTP_WRITE("'}");
        HTTP_DONE();
        free(uuid);
        free(proc_uuid);
        return;
    }
    size_t written = 0;
    multipart_itr(image) {
        if (image->name) {
            char *c = strdup(image->name);
            char last = 0;
            for(size_t i=0; c[i]; i++) {
                if (c[i] == '.' && last == '.') {
                    c[i] = c[i-1] = '-';
                }
                last = c[i];
            }
            char *img = dirappend(proc_uuid, c);
            int fd = open(img, O_RDWR|O_CREAT, 0777);
            if (fd != -1) {
                write(fd, image->data, image->data_len);
                close(fd);
                written++;
            }
            free(img);
            free(c);
        }
    }
    free(proc_uuid);
    if (written != 2) {
        HTTP_STATUS(200, "OK", "text/html");
        HTTP_WRITE(
                "<html><head><meta http-equiv=\"refresh\" content=\"0; url='/404'/>"
                "</head><body><h1>ERROR</h1></body></html>"
                );
        free(uuid);
        HTTP_DONE();
        free(uuid);
        return;
    }

    pthread_mutex_lock(&queue_lock);
    printf("LOCKED AT: %i\n", __LINE__);
    exec_queue *fnew = calloc(sizeof(exec_queue), 1);
    fnew->path = strdup(uuid);
    fnew->next = NULL;
    if (head) {
        tail->next = fnew;
        tail = fnew;
    } else {
        head = fnew;
        tail = fnew;
    }
    printf("UNLOCKED AT: %i\n", __LINE__);
    pthread_mutex_unlock(&queue_lock);

    HTTP_STATUS(200, "OK", "text/html");
    //HTTP_WRITE("<html><head><meta http-equiv=\"refresh\" content=\"0; url=/view/");
    //HTTP_WRITE(uuid);
    //HTTP_WRITE("\" /></head><body><h1>redirecting to image page</h1></body></html>");
    HTTP_WRITE("<html><body>click<a href='/view/");
    HTTP_WRITE(uuid);
    HTTP_WRITE("'>here</a> to see images</body></html>");
    free(uuid);
    HTTP_DONE();
}

HTTP(submit) {
    HTTP_STATUS(200, "OK", "text/html");
    HTTP_FILE("post.html");
    HTTP_DONE();
}

HTTP(landing) {
    HTTP_STATUS(200, "OK", "text/html");
    HTTP_FILE("index.html");
    HTTP_DONE();
}

bool dir_exists(char *c) {
    if (strlen(c) != 6) {
        return false;
    }
    char *p = dirappend("processing", c);
    char *pp = dirappend(p, "");
    free(p);
    if (0 != access(pp, F_OK)) {
        if (ENOENT == errno) {
            free(pp);
            return false;
        }
        if (ENOTDIR == errno) {
            free(pp);
            return false;
        }
    }
    free(pp);
    return true;
}

HTTP(send_image) {
    string *proc = api->first;
    list *img_l = api->rest;
    string *img = img_l->first;

    char *c = dirappend("processing", proc->str);
    char *cc = dirappend(c, img->str);
    free(c);
    if(access(cc, F_OK) != -1) {
        HTTP_STATUS(200, "OK", "image/jpeg");
        HTTP_FILE(cc);
    } else {
        HTTP_STATUS(404, "NOT_FOUND", "text/plain");
        HTTP_WRITE("waiting");
    }
    free(cc);
    HTTP_DONE();
}

HTTP(showprogress) {
    string *api_path = api->first;
    HTTP_STATUS(200, "OK", "text/html");
    size_t status = get_queue_status(api_path->str);
    if (status == -1) {
        if (dir_exists(api_path->str)) {
            HTTP_WRITE(
                    "<html><body><h1>"
                    "Your images are being processed, refresh for updates"
                    "</h1><br /><hr /><br />"
                    );
            HTTP_WRITE("<img style='height:100px' src='/images/");
            HTTP_WRITE(api_path->str);
            HTTP_WRITE("/result.jpg'><br />");
            char chr[4] = {'1', '0', '0', 0};
            for(int i=1;i<10;i++) {
                HTTP_WRITE("<img style='height:100px' src='/images/");
                HTTP_WRITE(api_path->str);
                HTTP_WRITE("/result_");
                chr[0] = '0'+i;
                HTTP_WRITE(chr);
                HTTP_WRITE(".jpg'><br />");
            }
            HTTP_WRITE("<img style='height:100px' src='/images/");
            HTTP_WRITE(api_path->str);
            HTTP_WRITE("/style.jpg'></br />");
            
            HTTP_WRITE("<img style='height:100px' src='/images/");
            HTTP_WRITE(api_path->str);
            HTTP_WRITE("/source.jpg'><br />");
            
            HTTP_WRITE("</body></html>");
        } else {
            HTTP_WRITE("Nothing to see here...");
        }
    } else {
        char buffer[10] = {0};
        snprintf(buffer, 10, "%d", status);
        HTTP_WRITE("<html><body><h1>Please be patient. Your spot in queue is ");
        HTTP_WRITE(buffer);
        HTTP_WRITE("</h1></body></html>");
    }
    HTTP_DONE();
}

void *run_cmds(void *worthless) {
    exec_queue *n = NULL;
    while(1) {
        pthread_mutex_lock(&queue_lock);
        n = head;
        if (n) {
            printf("LOCKED AT: %i\n", __LINE__);
            head = head->next;
        }
        pthread_mutex_unlock(&queue_lock);

        if (n) {
            printf("UNLOCKED AT: %i\n", __LINE__);
            char exec[20] = {0};
            snprintf(exec, 20, "./exec %s", n->path);
            system(exec);
            free(n->path);
            free(n);
        } else {
            usleep(10000);
        }
    }
}

int main(int argc, char **argv) {
    pthread_mutex_init(&queue_lock, NULL);
    pthread_t executor;
    pthread_create(&executor, NULL, run_cmds, NULL);
    srand(time(NULL));
#ifdef DEBUG
    puts("enabling debug mode");
    init_mem_tester();
#endif
    int port = 8088;
    if (argc == 2) {
        port = atoi(argv[1]);
    }

    HTTP_ROUTE(example) {
        PATH("/", landing);
        PATH("/submit", submit);
        PATH("/post/img", recieveimages);
        PATH("/view/_var", showprogress);
        PATH("/images/_var/_var", send_image);
    }
    http_t *http = create_server(example, port);
    start_http_server(http);
}
