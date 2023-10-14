#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#define MSG_LEN 1024 
#define MAX_CLIENTS 100

// Structure pour représenter un client dans la liste chaînée
typedef struct ClientNode {
    struct pollfd pfd;                      // Structure pour surveiller le socket du client
    struct sockaddr_storage client_addr;    // Stockage de l'adresse du client
    struct ClientNode* next;                // Pointeur vers le prochaine client dans la liste
} ClientNode;

ClientNode* head = NULL;  // Tête de la liste chaînée des clients

// Fonction pour ajouter un nouveau client à la liste chaînée
ClientNode* add_new_client(int fd, struct sockaddr* addr) {
    // Allouer de la mémoire pour le nouveau nœud client
    ClientNode* new_node = (ClientNode*)malloc(sizeof(ClientNode));
    if (!new_node) {
        perror("Échec d'allocation mémoire pour le nouveau client");
        exit(EXIT_FAILURE);
    }

    // Initialiser les valeurs du nouveau nœud
    new_node->pfd.fd = fd;
    new_node->pfd.events = POLLIN;  // Mettre en place l'événement POLLIN (données en attente de lecture)
    if (addr) {
        memcpy(&(new_node->client_addr), addr, sizeof(struct sockaddr_storage));
    } else {
        memset(&(new_node->client_addr), 0, sizeof(struct sockaddr_storage));
    }
    new_node->next = head;
    head = new_node;

    return new_node;
}

// Fonction pour supprimer un client de la liste chaînée
void remove_client(ClientNode* node) {
    // Si le nœud à supprimer est la tête, déplacer la tête
    if (head == node) {
        head = node->next;
    } else {
        // Parcourrir la liste jusqu'au nœud à supprimer
        ClientNode* tmp = head;
        while (tmp->next && tmp->next != node) {
            tmp = tmp->next;
        }
        if (tmp->next) {
            tmp->next = node->next;
        }
    }
    // Libérer la mémoire du nœud
    free(node);
}

// Fonction pour renvoyer les messages reçus aux clients
int echo_server(int sockfd) {
    char buff[MSG_LEN];  
    int bytes_received = recv(sockfd, buff, MSG_LEN, 0);  

    if (bytes_received <= 0 || strcmp(buff, "/quit\n") == 0) {
        printf("Le client s'est déconnecté ou une erreur est survenue.\n");
        close(sockfd);  
        return -1;
    }

    
    printf("Reçu: %s", buff);
    send(sockfd, buff, strlen(buff), 0);
    printf("Message envoyé !\n");
    return bytes_received;
}

// Fonction pour effectuer le binding du socket du serveur
int handle_bind(const char *port) {
    struct addrinfo hints, *result, *rp;
    int sfd;

    // Préparer la structure de l'adresse avec getaddrinfo()
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     // Autoriser IPv4 et IPv6
    hints.ai_socktype = SOCK_STREAM; // Utiliser TCP
    hints.ai_flags = AI_PASSIVE;     // Pour les adresses de bind

    if (getaddrinfo(NULL, port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    // Parcourir les résultats retournés par getaddrinfo() et effectuer le bind
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // Succès
        }
        close(sfd);  // Sinon, fermer le socket et essayer le suivant
    }

    // Si aucun bind n'a réussi
    if (rp == NULL) {
        fprintf(stderr, "Liaison échouée\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);  // Libérer la mémoire allouée par getaddrinfo()

    return sfd;
}
// Gérer une nouvelle connexion au serveur
void handle_new_connection(int sfd) {
    struct sockaddr_storage cli_addr;
    socklen_t len = sizeof(cli_addr);

    // Accepter la nouvelle connexion
    int connfd = accept(sfd, (struct sockaddr*)&cli_addr, &len);
    if (connfd < 0) {
        perror("accept()");
        return;
    }

    // Ajouter le nouveau client à la liste
    add_new_client(connfd, (struct sockaddr*)&cli_addr);
}

// Gérer le message reçu d'un client
void handle_client_message(ClientNode* client) {
    int bytes_received = echo_server(client->pfd.fd);
    if (bytes_received <= 0) {
        // Si le client s'est déconnecté ou qu'une erreur s'est produite, fermer le socket et le supprimer de la liste
        close(client->pfd.fd);
        remove_client(client);
    }
}


void poll_loop(int sfd) {
    while (1) {
        // Préparer les tableaux pour poll()
        struct pollfd pfds[MAX_CLIENTS + 1];
        ClientNode* client_nodes[MAX_CLIENTS + 1];

        pfds[0].fd = sfd;
        pfds[0].events = POLLIN;  
        client_nodes[0] = NULL;  

        // Remplir les tableaux avec les fd des clients
        int idx = 1;
        for (ClientNode* tmp = head; tmp && idx < MAX_CLIENTS + 1; tmp = tmp->next) {
            pfds[idx] = tmp->pfd;
            client_nodes[idx] = tmp;
            idx++;
        }

        // Utiliser poll() pour attendre un événement sur l'un des sockets
        int active_fds = poll(pfds, idx, -1);  // -1 signifie que poll() attend indéfiniment
        if (active_fds == -1) {
            perror("poll()");
            exit(EXIT_FAILURE);
        }

        // Parcourir les fd pour voir lesquels sont actifs
        for (int i = 0; i < idx && active_fds > 0; i++) {
            if (!(pfds[i].revents & POLLIN)) {  // Si l'événement POLLIN n'est pas activé, continuez
                continue;
            }
            active_fds--;  // Réduire le nombre de fd actifs à traiter

            if (pfds[i].fd == sfd) {
                // S'il s'agit du socket du serveur, cela signifie qu'une nouvelle connexion est en attente
                handle_new_connection(sfd);
            } else {
                // Sinon, c'est un mesage d'un client
                handle_client_message(client_nodes[i]);
            }
        }
    }
}




int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Utilisation : %s <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_bind(argv[1]);

    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()\n");
        exit(EXIT_FAILURE);
    }

    poll_loop(sfd);

    close(sfd);
    exit(EXIT_SUCCESS);
}
