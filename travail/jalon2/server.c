#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include "msg_struct.h"

#define MSG_LEN 1024
#define MAX_CLIENTS 100

typedef struct ClientNode {
    struct pollfd pfd;
    struct sockaddr_storage client_addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    struct ClientNode* next;
} ClientNode;

ClientNode* head = NULL;

ClientNode* add_new_client(int fd, struct sockaddr* addr) {
    ClientNode* new_node = (ClientNode*)malloc(sizeof(ClientNode));
    if (!new_node) {
        perror("Échec d'allocation mémoire pour le nouveau client");
        exit(EXIT_FAILURE);
    }
    new_node->connection_time = time(NULL);
    new_node->pfd.fd = fd;
    new_node->pfd.events = POLLIN;
    if (addr) {
        memcpy(&(new_node->client_addr), addr, sizeof(struct sockaddr_storage));
    } else {
        memset(&(new_node->client_addr), 0, sizeof(struct sockaddr_storage));
    }
    new_node->next = head;
    head = new_node;

    return new_node;
}

void remove_client(ClientNode* node) {
    if (head == node) {
        head = node->next;
    } else {
        ClientNode* tmp = head;
        while (tmp->next && tmp->next != node) {
            tmp = tmp->next;
        }
        if (tmp->next) {
            tmp->next = node->next;
        }
    }
    free(node);
}

void send_message(int sockfd, struct message* msg, const char* content) {
    send(sockfd, msg, sizeof(struct message), 0);
    send(sockfd, content, msg->pld_len, 0);
}

int is_nickname_taken(const char* nickname, ClientNode* current_client) {
    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (tmp != current_client && strcmp(nickname, tmp->nickname) == 0) {
            return 1;
        }
    }
    return 0;
}

void handle_nick_change(ClientNode* client, const char* new_nickname) {
    if (is_nickname_taken(new_nickname, client)) {
        // Le nouveau pseudonyme est déjà pris, renvoyez un message d'erreur.
        struct message response_msg;
        response_msg.type = NICKNAME_DOUBLON;
        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);

        // Déconnectez le client
        printf("Le client %s a tenté de prendre un pseudonyme déjà utilisé.\n", client->nickname);
        close(client->pfd.fd);
        remove_client(client);
        return; // Ajouté pour sortir de la fonction après avoir déconnecté le client
    } else {
        // Mettez à jour le pseudonyme de l'utilisateur et envoyez une confirmation.
        struct message response_msg;
        response_msg.type = NICKNAME_CHANGEMENT;
        strncpy(response_msg.nick_sender, client->nickname, NICK_LEN - 1); // Utilisez le pseudonyme actuel de l'utilisateur comme "nick_sender"
        strncpy(response_msg.infos, new_nickname, NICK_LEN - 1); // Utilisez le nouveau pseudonyme dans "infos"

        // Affichez un message dans la console du serveur pour indiquer le changement de pseudonyme
        printf("%s s'est renommé en %s\n", client->nickname, new_nickname);

        // Changez le pseudonyme de l'utilisateur dans la structure ClientNode
        strncpy(client->nickname, new_nickname, NICK_LEN - 1);

        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);

        // Ensuite, informez tous les autres utilisateurs du changement de pseudonyme
        for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
            if (tmp != client) {
                struct message notification_msg;
                notification_msg.type = NICKNAME_CHANGEMENT;
                strncpy(notification_msg.nick_sender, client->nickname, NICK_LEN - 1);
                strncpy(notification_msg.infos, new_nickname, NICK_LEN - 1);
                send(tmp->pfd.fd, &notification_msg, sizeof(notification_msg), 0);
            }
        }
    }
}

void handle_who_request(ClientNode* client) {
    struct message msgstruct;
    msgstruct.type = NICKNAME_LIST;
    msgstruct.nick_sender[0] = '\0';
    msgstruct.infos[0] = '\0';

    char online_users[NICK_LEN * MAX_CLIENTS] = {0};
    char* current_position = online_users; // Pointeur pour ajouter des noms d'utilisateur

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        // Ajoutez un nom d'utilisateur suivi d'un saut de ligne
        snprintf(current_position, sizeof(online_users) - (current_position - online_users), " - %s\n", tmp->nickname);
        current_position += strlen(tmp->nickname) + 4; // Avancez le pointeur
    }

    strncpy(msgstruct.infos, online_users, sizeof(msgstruct.infos) - 1);

    if (send(client->pfd.fd, &msgstruct, sizeof(msgstruct), 0) < sizeof(msgstruct)) {
        perror("send()");
    }
}


void handle_whois_request(ClientNode* client, const char* target_nickname) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(response_msg));
    response_msg.type = NICKNAME_INFOS;

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(target_nickname, tmp->nickname) == 0) {
            char info_message[INFOS_LEN];
            struct sockaddr_in* ipv4_addr = (struct sockaddr_in*)&(tmp->client_addr);
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(ipv4_addr->sin_addr), ip_str, INET_ADDRSTRLEN);

            // Formatage de la date et de l'heure
            char time_str[64];
            struct tm* localtime_info = localtime(&(tmp->connection_time));
            strftime(time_str, sizeof(time_str), "%Y/%m/%d@%H:%M", localtime_info);
                
            snprintf(info_message, INFOS_LEN, "[Server] : %s connected since %s with IP address %s and port number %d", 
                    target_nickname, time_str, ip_str, ntohs(ipv4_addr->sin_port));
            
            strncpy(response_msg.infos, info_message, INFOS_LEN - 1);
            send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
            return;
        }
    }

    // Si le pseudonyme n'est pas trouvé
    snprintf(response_msg.infos, INFOS_LEN, "[Server] : User %s not found.", target_nickname);
    send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
}

void broadcast_message(ClientNode* sender, const char* message) {
    struct message broadcast_msg;
    memset(&broadcast_msg, 0, sizeof(broadcast_msg));
    
    broadcast_msg.type = BROADCAST_SEND;
    strncpy(broadcast_msg.nick_sender, sender->nickname, NICK_LEN - 1);

    // Mettez le texte du message dans le champ "infos"
    strncpy(broadcast_msg.infos, message, INFOS_LEN - 1);
    
    // Définissez la longueur du payload à 0
    broadcast_msg.pld_len = 0;

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (tmp != sender) {
            send(tmp->pfd.fd, &broadcast_msg, sizeof(broadcast_msg), 0);
        }
    }
}

void handle_private_message(ClientNode* sender, const char* target_nickname, const char* message) {
    struct message msgstruct;
    memset(&msgstruct, 0, sizeof(msgstruct));
    msgstruct.pld_len = strlen(message);
    strncpy(msgstruct.nick_sender, sender->nickname, NICK_LEN - 1); // Utilisez le pseudonyme de l'expéditeur
    msgstruct.type = UNICAST_SEND;
    strncpy(msgstruct.infos, message, INFOS_LEN - 1);

    // Trouver le client destinataire
    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->nickname, target_nickname) == 0) {
            send_message(tmp->pfd.fd, &msgstruct, message);
            printf("Message privé envoyé de %s à %s : %s\n", sender->nickname, target_nickname, message);
            return;
        }
    }
    // Si le destinataire n'est pas trouvé, informez l'expéditeur
    char errorMsg[] = "Erreur: Destinataire non trouvé.";
    msgstruct.pld_len = strlen(errorMsg);
    strncpy(msgstruct.infos, errorMsg, INFOS_LEN - 1);
    send_message(sender->pfd.fd, &msgstruct, errorMsg);
    printf("Destinataire %s non trouvé. Message de %s non livré: %s\n", target_nickname, sender->nickname, message);
}


void handle_new_connection(int sfd) {
    struct sockaddr_storage cli_addr;
    socklen_t len = sizeof(cli_addr);
    struct message msg;

    int connfd = accept(sfd, (struct sockaddr*)&cli_addr, &len);
    if (connfd < 0) {
        perror("accept()");
        return;
    }

    int bytes_received = recv(connfd, &msg, sizeof(msg), 0);
    if (bytes_received <= 0 || msg.type != NICKNAME_NEW) {
        printf("Le client n'a pas fourni un pseudo correctement.\n");
        close(connfd);
        return;
    }

    if (is_nickname_taken(msg.nick_sender, NULL)) {
        printf("Le pseudonyme %s est déjà pris.\n", msg.nick_sender);

        struct message response_msg;
        response_msg.type = NICKNAME_DOUBLON;
        send(connfd, &response_msg, sizeof(response_msg), 0);

        close(connfd);
        return;
    }

    ClientNode* new_client = add_new_client(connfd, (struct sockaddr*)&cli_addr);
    strncpy(new_client->nickname, msg.nick_sender, NICK_LEN - 1);

    printf("Bienvenue sur le serveur, %s!\n", new_client->nickname);
}

int handle_bind(const char* port) {
    struct addrinfo hints, *res;
    int sfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    sfd = socket(res->ai_family, res->ai_socktype, 0);
    if (sfd == -1) {
        perror("socket");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    if (bind(sfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("bind");
        close(sfd);
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(res);
    return sfd;
}

void echo_server(ClientNode* client) {
    struct message msgstruct;
    char buff[MSG_LEN];
    int bytes_received;

    memset(buff, 0, sizeof(buff));

    bytes_received = recv(client->pfd.fd, &msgstruct, sizeof(msgstruct), 0);
    if (bytes_received <= 0) {
        printf("Le client s'est déconnecté ou une erreur est survenue.\n");
        close(client->pfd.fd);
        remove_client(client);
        return;
    }
    if (msgstruct.type == NICKNAME_NEW) {
        strncpy(client->nickname, msgstruct.nick_sender, NICK_LEN - 1);
    } else if (msgstruct.type == NICKNAME_CHANGEMENT) {
        const char* new_nickname = msgstruct.infos; // Utilisez le champ "infos" pour le nouveau pseudo
        handle_nick_change(client, new_nickname);
    } else if (msgstruct.type == NICKNAME_LIST) {
        handle_who_request(client);
    } else if (msgstruct.type == NICKNAME_INFOS) {
        const char* target_nickname = msgstruct.infos;
        handle_whois_request(client, target_nickname);
    } else if (msgstruct.type == BROADCAST_SEND) {
        printf("Broadcast from %s: %s\n", client->nickname, msgstruct.infos);
        broadcast_message(client, msgstruct.infos);
    } else if (msgstruct.type == UNICAST_SEND) {
        char target_nickname[NICK_LEN];
        char private_message[INFOS_LEN];
        strncpy(target_nickname, msgstruct.infos, NICK_LEN - 1); // Récupérez le destinataire du message
        target_nickname[NICK_LEN-1] = '\0'; // Assurez-vous que la chaîne est correctement terminée
        // Récupérez le message privé
        recv(client->pfd.fd, private_message, msgstruct.pld_len, 0);
        private_message[msgstruct.pld_len] = '\0'; // Assurez-vous que le message est correctement terminé
        printf("Private message from %s to %s: %s\n", msgstruct.nick_sender, target_nickname, private_message);
        // Traitez le message privé en appelant votre fonction handle_private_message
        handle_private_message(client, target_nickname, private_message);
    } else {
        char received_msg[MSG_LEN];
        memset(received_msg, 0, sizeof(received_msg));

        // Recevez le contenu du message
        bytes_received = recv(client->pfd.fd, received_msg, msgstruct.pld_len, 0);
        if (bytes_received <= 0) {
            printf("Le client s'est déconnecté ou une erreur est survenue.\n");
            close(client->pfd.fd);
            remove_client(client);
            return;
        }

        printf("pld_len: %i / nick_sender: %s / type: %s, infos %s:\n", msgstruct.pld_len, msgstruct.nick_sender, msg_type_str[msgstruct.type], msgstruct.infos);
        printf("Reçu: %s\n", received_msg);

        // Répondez au client en renvoyant le même message
        if (send(client->pfd.fd, &msgstruct, sizeof(msgstruct), 0) < sizeof(msgstruct) || send(client->pfd.fd, received_msg, msgstruct.pld_len, 0) < msgstruct.pld_len) {
            perror("send()");
        } else {
            printf("Message envoyé !\n");
        }
    }
}

void poll_loop(int sfd) {
    while (1) {
        struct pollfd pfds[MAX_CLIENTS + 1];
        ClientNode* client_nodes[MAX_CLIENTS + 1];

        pfds[0].fd = sfd;
        pfds[0].events = POLLIN;
        client_nodes[0] = NULL;

        int idx = 1;
        for (ClientNode* tmp = head; tmp && idx < MAX_CLIENTS + 1; tmp = tmp->next) {
            pfds[idx] = tmp->pfd;
            client_nodes[idx] = tmp;
            idx++;
        }

        int active_fds = poll(pfds, idx, -1);
        if (active_fds == -1) {
            perror("poll()");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < idx && active_fds > 0; i++) {
            if (pfds[i].revents & POLLIN) {
                if (pfds[i].fd == sfd) {
                    handle_new_connection(sfd);
                } else {
                    echo_server(client_nodes[i]);
                }
                active_fds--;
            } else if (pfds[i].revents & (POLLERR | POLLHUP)) {
                close(pfds[i].fd);
                remove_client(client_nodes[i]);
                active_fds--;
            }
        }
    }
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Utilisation : %s <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_bind(argv[1]);

    if (listen(sfd, SOMAXCONN) != 0) {
        perror("listen()\n");
        exit(EXIT_FAILURE);
    }

    poll_loop(sfd);

    close(sfd);
    exit(EXIT_SUCCESS);
}
