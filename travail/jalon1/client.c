#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MSG_LEN 1024  // Taille maximale du message

// Fonction pour établir une connexion avec le serveur
int handle_connect(char* server_name, char* server_port) {
    struct addrinfo hints, *result, *rp;
    int sockfd;

    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;  
    hints.ai_socktype = SOCK_STREAM;  

   
    if (getaddrinfo(server_name, server_port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        close(sockfd);  
    }

    
    if (rp == NULL) {
        fprintf(stderr, "Impossible de se connecter\n");
        exit(EXIT_FAILURE);
    }

    
    freeaddrinfo(result);
    return sockfd;
}

// Fonction principale pour le client d'écho
void echo_client(int sockfd) {
    struct pollfd fds[2];
    char buff[MSG_LEN];

    
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    
    while (1) {
        
        int activity = poll(fds, 2, -1);
        if (activity < 0) {
            perror("Erreur de poll");
            exit(EXIT_FAILURE);
        }

        
        if (fds[0].revents & POLLIN) {
            printf("Message: ");
            fgets(buff, MSG_LEN, stdin);  

            
            if (strcmp(buff, "/quit\n") == 0) {
                close(sockfd);  // Fermer le socket
                printf("Déconnecté du serveur.\n");
                exit(EXIT_SUCCESS);
            }

           
            send(sockfd, buff, strlen(buff), 0);
            printf("Message envoyé !\n");
        }

        
        if (fds[1].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);  
            
            if (recv(sockfd, buff, MSG_LEN, 0) <= 0) {
                perror("Le serveur s'est déconnecté ou une erreur est survenue");
                exit(EXIT_FAILURE);
            }
            printf("Reçu: %s", buff);
        }
    }
}

// Fonction principale
int main(int argc, char* argv[]) {
    // Vérification du nombre d'arguments
    if (argc != 3) {
        fprintf(stderr, "Utilisation : %s <nom_serveur> <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Établir la connexion avec le serveur
    int sockfd = handle_connect(argv[1], argv[2]);
    // Entrer dans la boucle principale du client
    echo_client(sockfd);
    // Fermer le socket avant de quitter
    close(sockfd);
    return EXIT_SUCCESS;
}