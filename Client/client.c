#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#define SERVER_IP "127.0.0.1"
#define DEFAULT_PORT 2012
#define BUFFER_SIZE 1024

#define CODE_SUCCESS 0
#define CODE_FAILURE 1
#define CODE_TIMEOUT 2
#define CODE_RATE_LIMIT_EXCEEDED 3
#define CODE_SERVER_BUSY 4

int send_qr_code(int socket, const char *file_path) {
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Error opening file");
        return 0; // Return 0 to indicate failure
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Send file size to server
    if (send(socket, &file_size, sizeof(size_t), 0) < 0) {
        perror("Error sending file size");
        fclose(file);
        return 0; // Return 0 to indicate failure
    }

    // Send file contents to server
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(socket, buffer, bytes_read, 0) < 0) {
            perror("Error sending file contents");
            fclose(file);
            return 0; // Return 0 to indicate failure
        }
    }

    fclose(file);
    return 1; // Return 1 to indicate success
}

int receive_server_message(int socket, char *url) {
    int server_code;
    size_t url_length;

    if (recv(socket, &server_code, sizeof(server_code), 0) < 0) {
        perror("Error receiving server message");
        return 0;
    }

    switch (server_code) {
        case CODE_SUCCESS:
            printf("Server response: Success. URL received.\n");
            if (recv(socket, &url_length, sizeof(size_t), 0) < 0) {
                perror("Error receiving URL length");
                return 0;
            }
            if (recv(socket, url, url_length, 0) < 0) {
                perror("Error receiving URL");
                return 0;
            }
            url[url_length] = '\0'; // Null-terminate the URL string
            printf("URL: %s\n", url);
            break;
        case CODE_FAILURE:
            printf("Server response: Failure. Something went wrong and no URL is being returned.\n");
            break;
        case CODE_TIMEOUT:
            printf("Server response: Timeout. Connection closed.\n");
            close(socket);
            exit(EXIT_SUCCESS);
            break;
        case CODE_RATE_LIMIT_EXCEEDED:
            printf("Server response: Rate limit exceeded. Please try again later.\n");
            printf("Retrying after 60 seconds...\n");
            sleep(60);
            return 0;
            break;
        case CODE_SERVER_BUSY:
            printf("Server response: Server is busy. Please try again later.\n");
            close(socket);
            exit(EXIT_SUCCESS);
            break;
        default:
            printf("Unknown server response code.\n");
            return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    // Create socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    char file_path[256]; // Assuming the file path won't exceed 255 characters
    char url[BUFFER_SIZE]; // To store the received URL

    int quit_requested = 0; // Flag to track if the client has sent a 'q'

    while (!quit_requested) {
        int success = 0;
        int receiver_success = 0;

        while (!success && !quit_requested) {
            printf("Enter the path to the QR code image file (or enter 'q' to quit): ");
            scanf("%255s", file_path);

            // Check if the user wants to quit
            if (strcmp(file_path, "q") == 0) {
                // User entered 'q', send 'q' to server and set the quit_requested flag
                char quit_message[] = "q";
                size_t message_size = strlen(quit_message);
                if (send(client_socket, &message_size, sizeof(size_t), 0) < 0) {
                    perror("Error sending quit message size");
                    break;
                }

                if (send(client_socket, quit_message, message_size, 0) < 0) {
                    perror("Error sending quit message");
                    break;
                }

                quit_requested = 1; // Set the flag to indicate that 'q' has been sent
                break;
            }

            // Send QR code to server
            success = send_qr_code(client_socket, file_path);

            if (!success) {
                printf("Please try again.\n");
            }
        }

        // If 'q' was sent, don't receive and handle server message
        if (!quit_requested) {
            // Receive and handle server message
            receiver_success = receive_server_message(client_socket, url);
            if (!receiver_success) {
                printf("Please try again.\n");
            }
        }
    }

    // Close socket
    close(client_socket);

    return 0;
}
