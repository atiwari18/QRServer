#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define DEFAULT_PORT 2012
#define DEFAULT_RATE_MSGS 3
#define DEFAULT_RATE_TIME 60
#define DEFAULT_MAX_USERS 3
#define DEFAULT_TIMEOUT 80
#define BUFFER_SIZE 100000 // Increased buffer size
#define LOG_FILE "server_log.txt"
#define MAX_FILE_SIZE 1000000 // Maximum file size (1MB)

#define CODE_SUCCESS 0
#define CODE_FAILURE 1
#define CODE_TIMEOUT 2
#define CODE_RATE_LIMIT_EXCEEDED 3
#define CODE_SERVER_BUSY 4

typedef struct {
    char client_ip[INET_ADDRSTRLEN];
    time_t last_request_time;
    int request_count;
    int is_connected; // Track if client is connected
} ClientInfo;

ClientInfo client_info_map[256];

const char* timestamp() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[20];
    strftime(buffer, 20, "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// Global variable for shared memory ID
int shmid;
FILE *log_file;

void create_shared_memory() {
    // Create the shared memory segment
    if ((shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    // Initialize connected_users
    int* shared_memory = (int *) shmat(shmid, NULL, 0);
    if (shared_memory == (int *) -1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    *shared_memory = 0; // Initialize connected_users
    if (shmdt(shared_memory) == -1) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
}

void attach_shared_memory(int** shared_memory) {
    *shared_memory = (int *) shmat(shmid, NULL, 0);
    if (*shared_memory == (int *) -1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

void detach_shared_memory(int* shared_memory) {
    if (shmdt(shared_memory) == -1) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
}

void increment_connected_users() {
    int* shared_memory;
    attach_shared_memory(&shared_memory);
    (*shared_memory)++;
    detach_shared_memory(shared_memory);
}

void decrement_connected_users() {
    int* shared_memory;
    attach_shared_memory(&shared_memory);
    (*shared_memory)--;
    detach_shared_memory(shared_memory);
}

void reset_client_info(ClientInfo *client_info) {
    client_info->last_request_time = time(NULL);
    client_info->request_count = 0;
    client_info->is_connected = 0; // Reset is_connected flag
}

void handle_timeout(int client_socket) {
    int timeout_code = CODE_TIMEOUT;
    send(client_socket, &timeout_code, sizeof(timeout_code), 0);
    printf("Timeout occurred for client. Connection closed.\n");
    fprintf(log_file, "%s Timeout occurred for client. Connection closed.\n", timestamp());
}

void send_server_message(int client_socket, int return_code, const char *url) {
    size_t url_length = strlen(url);
    if (send(client_socket, &return_code, sizeof(int), 0) < 0) {
        perror("Error sending return code");
        return;
    }

    if (send(client_socket, &url_length, sizeof(size_t), 0) < 0) {
        perror("Error sending URL length");
        return;
    }

    if (send(client_socket, url, url_length, 0) < 0) {
        perror("Error sending URL");
        return;
    }
}

void handle_client(int client_socket, int rate_msgs, int rate_time, int timeout, int max_users, int server_socket, size_t max_file_size) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("Error forking process");
        close(client_socket);
        return;
    } else if (pid == 0) {
        close(server_socket);

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        getpeername(client_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        time_t last_interaction_time = time(NULL);

        // Check if the number of connected users has exceeded the max_users limit
        int* connected_users;

        while (1) {
            attach_shared_memory(&connected_users);

            printf("Connected Users: %d\n", *connected_users);
            fprintf(log_file, "%s Connected Users: %d\n", timestamp(), *connected_users);

            ClientInfo *client_info = NULL;
            for (int i = 0; i < 256; i++) {
                if (strcmp(client_info_map[i].client_ip, client_ip) == 0) {
                    client_info = &client_info_map[i];
                    break;
                }
            }
            if (!client_info) {
                for (int i = 0; i < 256; i++) {
                    if (client_info_map[i].client_ip[0] == '\0') {
                        client_info = &client_info_map[i];
                        strcpy(client_info->client_ip, client_ip);
                        client_info->last_request_time = time(NULL);
                        client_info->request_count = 0;
                        client_info->is_connected = 1; // Mark as connected
                        (*connected_users)++; // Increment connected users count
                        break;
                    }
                }
            }

            if (*connected_users > max_users) {
                printf("SENDING BUSY SERVER MESSAGE\n");
                fprintf(log_file, "%s SENDING BUSY SERVER MESSAGE\n", timestamp());
                // Server busy, send error message to client
                int server_busy_code = CODE_SERVER_BUSY;
                send(client_socket, &server_busy_code, sizeof(server_busy_code), 0);
                printf("Server busy. Connection from %s:%d terminated.\n", client_ip, ntohs(client_addr.sin_port));
                fprintf(log_file, "%s Server busy. Connection from %s:%d terminated.\n", timestamp(), client_ip, ntohs(client_addr.sin_port));
                break;
            }

            detach_shared_memory(connected_users);
            time_t current_time = time(NULL);
            time_t elapsed_time = current_time - last_interaction_time;

            if (elapsed_time > timeout) {
                handle_timeout(client_socket);
                break;
            }

            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_socket, &read_fds);
            struct timeval tv = { timeout, 0 };
            int select_ret = select(client_socket + 1, &read_fds, NULL, NULL, &tv);
            if (select_ret == 0) {
                handle_timeout(client_socket);
                break;
            } else if (select_ret < 0) {
                perror("Error in select");
                break;
            }

            for (int i = 0; i < 256; i++) {
                if (strcmp(client_info_map[i].client_ip, client_ip) == 0) {
                    client_info = &client_info_map[i];
                    break;
                }
            }
            if (!client_info) {
                for (int i = 0; i < 256; i++) {
                    if (client_info_map[i].client_ip[0] == '\0') {
                        client_info = &client_info_map[i];
                        strcpy(client_info->client_ip, client_ip);
                        client_info->last_request_time = time(NULL);
                        client_info->request_count = 0;
                        client_info->is_connected = 1; // Mark as connected
                        break;
                    }
                }
            }

            if (current_time - client_info->last_request_time > rate_time) {
                client_info->last_request_time = current_time;
                client_info->request_count = 0;
            }

            client_info->request_count++;

            if (client_info->request_count > rate_msgs) {
                int rate_limit_exceeded_code = CODE_RATE_LIMIT_EXCEEDED;
                send(client_socket, &rate_limit_exceeded_code, sizeof(rate_limit_exceeded_code), 0);
                sleep(rate_time);
                client_info->request_count = 0;
                continue;
            }

            size_t image_size;
            if (recv(client_socket, &image_size, sizeof(size_t), 0) < 0) {
                perror("Error receiving image size");
                break;
            } 

            printf("Received image size: %zu bytes\n", image_size);
            fprintf(log_file, "%s Received image size: %zu bytes\n", timestamp(), image_size);

            if (image_size == 1) {
                char quit_message;
                if (recv(client_socket, &quit_message, sizeof(char), 0) < 0) {
                    perror("Error receiving quit message");
                    break;
                }
                if (quit_message == 'q') {
                    printf("%s:%d has disconnected.\n", client_ip, ntohs(client_addr.sin_port));
                    fprintf(log_file, "%s %s:%d has disconnected.\n", timestamp(), client_ip, ntohs(client_addr.sin_port));
                    reset_client_info(client_info);
                    break;
                }
            }

            char buffer[BUFFER_SIZE];
            size_t total_bytes_received = 0;

            char temp_file_path[] = "/tmp/qrcode_image.png";
            FILE *image_file = fopen(temp_file_path, "wb+");
            if (!image_file) {
                perror("Error creating temporary file");
                break;
            }

            while (total_bytes_received < image_size) {
                ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
                if (bytes_received < 0) {
                    perror("Error receiving image data");
                    fclose(image_file);
                    break;
                }

                // Check if receiving more data exceeds max file size
                if (total_bytes_received + bytes_received > max_file_size) {
                    printf("Exceeded maximum file size\n");
                    fprintf(log_file, "%s Exceeded maximum file size\n", timestamp());
                    fclose(image_file);
                    remove(temp_file_path); // Delete the incomplete file
                    break;
                }

                fwrite(buffer, 1, bytes_received, image_file);
                total_bytes_received += bytes_received;
            }

            fclose(image_file);
            printf("Image reception completed\n");
            fprintf(log_file, "%s Image reception completed\n", timestamp());

            char command[512];
            snprintf(command, sizeof(command), "java -cp javase.jar:core.jar com.google.zxing.client.j2se.CommandLineRunner %s", temp_file_path);

            FILE *zxing_output = popen(command, "r");
            if (!zxing_output) {
                perror("Error running ZXing");
                break;
            }

            char zxing_output_buffer[1024];
            char *zxing_result = NULL;
            size_t zxing_result_len = 0;
            while (fgets(zxing_output_buffer, sizeof(zxing_output_buffer), zxing_output)!= NULL) {
                size_t buffer_len = strlen(zxing_output_buffer);
                char *temp = realloc(zxing_result, zxing_result_len + buffer_len + 1);
                if (temp == NULL) {
                    perror("Error reallocating memory");
                    // Handle error appropriately (e.g., free resources, exit program)
                    exit(EXIT_FAILURE);
                }
                zxing_result = temp;
                strcpy(zxing_result + zxing_result_len, zxing_output_buffer);
                zxing_result_len += buffer_len;
            }

            pclose(zxing_output);

            if (zxing_result == NULL) {
                perror("Error reading ZXing output");
                break;
            }

            char *parsed_result_line = strstr(zxing_result, "Parsed result:");
            if (parsed_result_line!= NULL) {
                char *url_start = strchr(parsed_result_line, ':');
                if (url_start!= NULL) {
                    url_start += 2;
                    char *url_end = strchr(url_start, '\n');
                    if (url_end!= NULL) {
                        *url_end = '\0';
                        if (client_info->request_count <= rate_msgs) {
                            send_server_message(client_socket, CODE_SUCCESS, url_start);
                        }
                    }
                } else {
                    printf("URL not found in ZXing output\n");
                    fprintf(log_file, "%s URL not found in ZXing output\n", timestamp());
                }
            } else {
                printf("Parsed result line not found in ZXing output\n");
                fprintf(log_file, "%s Parsed result line not found in ZXing output\n", timestamp());
            }

            free(zxing_result);

            client_info->last_request_time = time(NULL);
        }

        // Decrement connected users count only if the client was connected
        attach_shared_memory(&connected_users);
        if (*connected_users > 0) {
            (*connected_users)--;
        }
        detach_shared_memory(connected_users);

        exit(EXIT_SUCCESS);
    } else {
        return;
    }
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int rate_msgs = DEFAULT_RATE_MSGS;
    int rate_time = DEFAULT_RATE_TIME;
    int max_users = DEFAULT_MAX_USERS;
    int timeout = DEFAULT_TIMEOUT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-PORT") == 0) {
            if (i + 1 < argc) {
                port = atoi(argv[++i]);
                if (port < 2000 || port > 3000) {
                    fprintf(stderr, "Option -PORT requires an argument between 2000 & 3000\n");
                    exit(EXIT_FAILURE);
                }
            } else {
                fprintf(stderr, "Option -PORT requires an argument between 2000 & 3000\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-RATE") == 0) {
            if (i + 1 < argc) {
                rate_msgs = atoi(argv[++i]);
                rate_time = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Option -RATE requires two arguments.\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-MAX_USERS") == 0) {
            if (i + 1 < argc) {
                max_users = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Option -MAX_USERS requires an argument.\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-TIME_OUT") == 0) {
            if (i + 1 < argc) {
                timeout = atoi(argv[++i]);
            } else {
                fprintf(stderr, "Option -TIME_OUT requires an argument.\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -PORT [port] -RATE [msgs] [seconds] -MAX_USERS [users] -TIME_OUT [timeout]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    printf("Port: %d\n", port);
    printf("Rate messages: %d\n", rate_msgs);
    printf("Rate time: %d\n", rate_time);
    printf("Max users: %d\n", max_users);
    printf("Timeout: %d\n", timeout);

    log_file = fopen(LOG_FILE, "a");
    if (!log_file) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    fprintf(log_file, "%s Server listening on port %d...\n", timestamp(), port);

    create_shared_memory();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) < 0) {
        perror("Listening Failed");
        exit(EXIT_FAILURE);
    }

    fprintf(log_file, "%s Server listening on port %d...\n", timestamp(), port);

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            perror("Error in accepting connection");
            continue;
        }

        printf("New connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        fprintf(log_file, "%s New connection accepted from %s:%d\n", timestamp(), inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        handle_client(client_socket, rate_msgs, rate_time, timeout, max_users, server_socket, MAX_FILE_SIZE);
    }

    close(server_socket);
    fclose(log_file);

    return 0;
}
