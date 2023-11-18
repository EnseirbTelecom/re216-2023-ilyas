 //server.c
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
#include <stdbool.h>
#include "msg_struct.h"

#define MSG_LEN 1024
#define MAX_CLIENTS 100
#define CHANNEL_LEN 32
#define MAX_CHANNELS 100

typedef struct ClientNode {
    struct pollfd pfd;
    struct sockaddr_storage client_addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    char channel_name[CHANNEL_LEN];
    char file_transfer_sender[NICK_LEN];
    struct ClientNode* next;
} ClientNode;

typedef struct Channel {
    char name[INFOS_LEN]; 
} Channel;

ClientNode* head = NULL;
Channel channels[MAX_CHANNELS];
int channel_count = 0; 

// Fonction pour envoyer un message complet au client
ssize_t send_full_message(int server_fd, struct message* msg, const char* payload) {
    ssize_t total_sent = 0;
    ssize_t ret;
    int message_size = sizeof(struct message);

    while (total_sent != message_size) {
        ret = write(server_fd, (char *)msg + total_sent, message_size - total_sent);
        if (ret == -1) {
            perror("write");
            return -1;
        }
        total_sent += ret;
    }

    if (msg->pld_len > 0 && payload != NULL) {
        ssize_t payload_sent = 0;
        while (payload_sent != msg->pld_len) {
            ret = write(server_fd, payload + payload_sent, msg->pld_len - payload_sent);
            if (ret == -1) {
                perror("write payload");
                return -1;
            }
            payload_sent += ret;
        }
        total_sent += payload_sent;
    }

    return total_sent;
}

// Fonction pour recevoir un message complet du client
ssize_t receive_full_message(int client_fd, struct message* msg, char* payload) {
    ssize_t total_received = 0;
    ssize_t ret;
    int message_size = sizeof(struct message);

    while (total_received != message_size) {
        ret = read(client_fd, (char *)msg + total_received, message_size - total_received);
        if (ret == -1) {
            perror("read");
            return -1;
        }
        total_received += ret;
    }

    if (msg->pld_len > 0 && payload != NULL) {
        ssize_t payload_received = 0;
        while (payload_received != msg->pld_len) {
            ret = read(client_fd, payload + payload_received, msg->pld_len - payload_received);
            if (ret == -1) {
                perror("read payload");
                return -1;
            }
            payload_received += ret;
        }
        total_received += payload_received;
    }

    return total_received;
}

// Fonction pour ajouter un nouveau client à la liste des clients
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

// Fonction pour supprimer un client de la liste des clients
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

// Fonction pour vérifier si un pseudonyme est déjà pris par un autre client
int is_nickname_taken(const char* nickname, ClientNode* current_client) {
    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (tmp != current_client && strcmp(nickname, tmp->nickname) == 0) {
            return 1;
        }
    }
    return 0;
}

// Fonction pour gérer le changement de pseudonyme d'un client
void handle_nick_change(ClientNode* client, const char* new_nickname) {
    if (is_nickname_taken(new_nickname, client)) {
        
        struct message response_msg;
        response_msg.type = NICKNAME_DOUBLON;
        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);

        printf("Le client %s a tenté de prendre un pseudonyme déjà utilisé.\n", client->nickname);
        close(client->pfd.fd);
        remove_client(client);
        return;
    } else {
        struct message response_msg;
        response_msg.type = NICKNAME_CHANGEMENT;
        strncpy(response_msg.infos, new_nickname, NICK_LEN - 1);

        strncpy(client->nickname, new_nickname, NICK_LEN - 1);
        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
    }
}

// Fonction pour gérer la demande de liste des pseudonymes des clients
void handle_who_request(ClientNode* client) {
    struct message msgstruct;
    msgstruct.type = NICKNAME_LIST;
    msgstruct.nick_sender[0] = '\0';
    msgstruct.infos[0] = '\0';

    char online_users[NICK_LEN * MAX_CLIENTS] = {0};
    char* current_position = online_users;

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        snprintf(current_position, sizeof(online_users) - (current_position - online_users), " - %s\n", tmp->nickname);
        current_position += strlen(tmp->nickname) + 4; 
    }

    strncpy(msgstruct.infos, online_users, sizeof(msgstruct.infos) - 1);

    if (send(client->pfd.fd, &msgstruct, sizeof(msgstruct), 0) < sizeof(msgstruct)) {
        perror("send()");
    }
}

// Fonction pour gérer la demande d'informations sur un pseudonyme
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

    snprintf(response_msg.infos, INFOS_LEN, "[Server] : Destinataire non trouvé.\n");
    send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
}

// Fonction pour diffuser un message à tous les clients
void broadcast_message(ClientNode* sender, const char* message) {
    struct message broadcast_msg;
    memset(&broadcast_msg, 0, sizeof(broadcast_msg));
    
    broadcast_msg.type = BROADCAST_SEND;
    strncpy(broadcast_msg.nick_sender, sender->nickname, NICK_LEN - 1);

    strncpy(broadcast_msg.infos, message, INFOS_LEN - 1);
    
    broadcast_msg.pld_len = 0;

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (tmp != sender) {
            send(tmp->pfd.fd, &broadcast_msg, sizeof(broadcast_msg), 0);
        }
    }
}

// Fonction pour gérer l'envoi d'un message privé à un client spécifique
void handle_private_message(ClientNode* sender, const char* target_nickname, const char* message) {
    struct message msgstruct;
    memset(&msgstruct, 0, sizeof(msgstruct));
    msgstruct.pld_len = strlen(message);
    strncpy(msgstruct.nick_sender, sender->nickname, NICK_LEN - 1); // Utilisez le pseudonyme de l'expéditeur
    msgstruct.type = UNICAST_SEND;
    strncpy(msgstruct.infos, message, INFOS_LEN - 1);

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->nickname, target_nickname) == 0) {
            send(tmp->pfd.fd, &msgstruct, sizeof(struct message), 0);
            send(tmp->pfd.fd, message, msgstruct.pld_len, 0);
            printf("Message privé envoyé de %s à %s : %s\n", sender->nickname, target_nickname, message);
            return;
        }
    }

    char errorMsg[] = "Erreur: Destinataire non trouvé.";
    msgstruct.pld_len = strlen(errorMsg);
    strncpy(msgstruct.infos, errorMsg, INFOS_LEN - 1);
    
    send(sender->pfd.fd, &msgstruct, sizeof(struct message), 0);
    send(sender->pfd.fd, errorMsg, msgstruct.pld_len, 0);
    printf("Destinataire %s non trouvé. Message de %s non livré: %s\n", target_nickname, sender->nickname, message);
}

// Fonction pour vérifier si un salon existe déjà
int channel_exists(const char* channel_name) {
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].name, channel_name) == 0) {
            return 1; 
        }
    }
    return 0; 
}

// Fonction pour compter le nombre de clients dans un salon
int count_clients_in_channel(const char* channel_name) {
    int count = 0;
    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->channel_name, channel_name) == 0) {
            count++;
        }
    }
    return count;
}

// Fonction pour gérer la création d'un salon
void handle_create_channel(ClientNode* client, const char* channel_name) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(struct message));
    
    if (channel_exists(channel_name)) {
        response_msg.type = MULTICAST_CREATE_FAILED;
        snprintf(response_msg.infos, sizeof(response_msg.infos), "Erreur: Le salon '%s' existe déjà.", channel_name);
        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
        return;
    } else {
        snprintf(response_msg.infos, sizeof(response_msg.infos), "%s", channel_name);
        char previous_channel[CHANNEL_LEN];
        strncpy(previous_channel, client->channel_name, CHANNEL_LEN);
        bool previousChannelDeleted = false;

        if (client->channel_name[0] != '\0') {
            memset(client->channel_name, 0, CHANNEL_LEN);

            if (count_clients_in_channel(previous_channel) == 0) {
                for (int i = 0; i < channel_count; i++) {
                    if (strcmp(channels[i].name, previous_channel) == 0) {
                        for (int j = i; j < channel_count - 1; j++) {
                            strcpy(channels[j].name, channels[j + 1].name);
                        }
                        channel_count--;
                        printf("Salon '%s' supprimé car vide.\n", previous_channel);
                        previousChannelDeleted = true;
                        break;
                    }
                }
            }
        }

        strncpy(channels[channel_count].name, channel_name, INFOS_LEN - 1);
        channels[channel_count].name[INFOS_LEN - 1] = '\0';
        channel_count++;

        strncpy(client->channel_name, channel_name, CHANNEL_LEN - 1);
        client->channel_name[CHANNEL_LEN - 1] = '\0';

        if (previousChannelDeleted) {
            response_msg.type = MULTICAST_CREATE_QUIT;
        } else {
            response_msg.type = MULTICAST_CREATE;
        }

        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
    }
}

// Fonction pour gérer la demande de liste des salons
void handle_channel_list_request(ClientNode* client) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(struct message));
    response_msg.type = MULTICAST_LIST; 

    char channels_list[MAX_CHANNELS * (CHANNEL_LEN + 4)] = {0}; 
    char* current_position = channels_list;

    current_position += snprintf(current_position, sizeof(channels_list), "[Server]: Liste des salons:\n");

    for (int i = 0; i < channel_count; ++i) {
        current_position += snprintf(current_position, sizeof(channels_list) - (current_position - channels_list), "                          - %s\n", channels[i].name);
    }

    strncpy(response_msg.infos, channels_list, sizeof(response_msg.infos) - 1);

    if (send(client->pfd.fd, &response_msg, sizeof(response_msg), 0) < sizeof(response_msg)) {
        perror("send()");
    }
}

// Fonction pour notifier les membres d'un salon
void notify_channel_members(const char* channel_name, const char* message, ClientNode* exclude_client) {
    struct message notification_msg;
    memset(&notification_msg, 0, sizeof(notification_msg));
    notification_msg.type = MULTICAST_NOTIFICATION; 

    strncpy(notification_msg.infos, message, INFOS_LEN - 1);
    strncpy(notification_msg.nick_sender, channel_name, CHANNEL_LEN - 1);

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->channel_name, channel_name) == 0 && tmp != exclude_client) {
            send(tmp->pfd.fd, &notification_msg, sizeof(notification_msg), 0);
        }
    }
}

// Fonction pour gérer la sortie d'un client d'un salon
void handle_multicast_quit(ClientNode* client, const char* channel_name) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(response_msg));
    response_msg.type = MULTICAST_QUIT;

    if (strcmp(client->channel_name, channel_name) == 0) {
        send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
        char quit_message[INFOS_LEN];
        snprintf(quit_message, INFOS_LEN, " %s a quitté le salon.", client->nickname);
        notify_channel_members(channel_name, quit_message, client);

        memset(client->channel_name, 0, CHANNEL_LEN);
        printf("Le client %s a quitté le salon '%s'.\n", client->nickname, channel_name);

        if (count_clients_in_channel(channel_name) == 0) {
            for (int i = 0; i < channel_count; i++) {
                if (strcmp(channels[i].name, channel_name) == 0) {
                    for (int j = i; j < channel_count - 1; j++) {
                        strcpy(channels[j].name, channels[j + 1].name);
                    }
                    channel_count--;
                    printf("Salon '%s' supprimé car vide.\n", channel_name);
                    snprintf(response_msg.infos, sizeof(response_msg.infos), "Vous avez quitté le salon '%s', qui a été supprimé car vous étiez le dernier membre.", channel_name);
                    break;
                }
            }
        } else {
            snprintf(response_msg.infos, sizeof(response_msg.infos), "Vous avez quitté le salon '%s'.", channel_name);
        }
    } else {
        snprintf(response_msg.infos, sizeof(response_msg.infos), "Erreur : Vous n'êtes pas dans le salon '%s'.", channel_name);
    }

    send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
}

// Fonction pour gérer l'adhésion d'un client à un salon
void handle_multicast_join(ClientNode* client, const char* channel_name) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(struct message));
    response_msg.type = MULTICAST_JOIN;

    char previous_channel[CHANNEL_LEN];
    strncpy(previous_channel, client->channel_name, CHANNEL_LEN);
    bool previousChannelDeleted = false;

    if (client->channel_name[0] != '\0') {
        memset(client->channel_name, 0, CHANNEL_LEN);

        if (count_clients_in_channel(previous_channel) == 0) {
            for (int i = 0; i < channel_count; i++) {
                if (strcmp(channels[i].name, previous_channel) == 0) {
                    for (int j = i; j < channel_count - 1; j++) {
                        strcpy(channels[j].name, channels[j + 1].name);
                    }
                    channel_count--;
                    printf("Salon '%s' supprimé car vide.\n", previous_channel);
                    previousChannelDeleted = true;
                    break;
                }
            }
        }
    }

    if (channel_exists(channel_name)) {
        strncpy(client->channel_name, channel_name, CHANNEL_LEN - 1);
        client->channel_name[CHANNEL_LEN - 1] = '\0';

        if (previousChannelDeleted) {
            snprintf(response_msg.infos, sizeof(response_msg.infos), "Vous avez rejoint le salon '%s'. Votre ancien salon '%s' a été supprimé car il était vide.", channel_name, previous_channel);
        } else {
            snprintf(response_msg.infos, sizeof(response_msg.infos), "Vous avez rejoint le salon '%s'.", channel_name);
        }
        
        char join_message[INFOS_LEN];
        snprintf(join_message, INFOS_LEN, "%s a rejoint le salon", client->nickname);
        notify_channel_members(channel_name, join_message, client);
    } else {
        snprintf(response_msg.infos, sizeof(response_msg.infos), "Erreur : Le salon '%s' n'existe pas.", channel_name);
    }

    send(client->pfd.fd, &response_msg, sizeof(response_msg), 0);
}

// Fonction pour gérer une demande de transfert de fichier
void handle_file_request(ClientNode* sender, const char* target_nickname, const char* file_path) {
    struct message msgstruct;
    memset(&msgstruct, 0, sizeof(msgstruct));
    msgstruct.type = FILE_REQUEST;
    strncpy(msgstruct.nick_sender, sender->nickname, NICK_LEN - 1);
    msgstruct.pld_len = strlen(file_path);

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->nickname, target_nickname) == 0) {
            send(tmp->pfd.fd, &msgstruct, sizeof(struct message), 0);
            send(tmp->pfd.fd, file_path, msgstruct.pld_len, 0);

            strncpy(tmp->file_transfer_sender, sender->nickname, NICK_LEN - 1);
            return;
        }
    }
}

// Fonction pour gérer la réponse à une demande de transfert de fichier
void handle_file_response(ClientNode* client, enum msg_type response_type) {
    struct message response_msg;
    memset(&response_msg, 0, sizeof(response_msg));
    response_msg.type = response_type;
    strncpy(response_msg.nick_sender, client->nickname, NICK_LEN - 1);

    for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
        if (strcmp(tmp->nickname, client->file_transfer_sender) == 0) {
            send(tmp->pfd.fd, &response_msg, sizeof(response_msg), 0);
            memset(client->file_transfer_sender, 0, NICK_LEN);
            return;
        }
    }
}

// Fonction pour gérer une nouvelle connexion de client
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
        struct message response_msg;
        response_msg.type = NICKNAME_DOUBLON;
        send(connfd, &response_msg, sizeof(response_msg), 0);
        close(connfd);
        return;
    }

    ClientNode* new_client = add_new_client(connfd, (struct sockaddr*)&cli_addr);
    strncpy(new_client->nickname, msg.nick_sender, NICK_LEN - 1);

    struct message response_msg;
    response_msg.type = NICKNAME_NEW;
    strncpy(response_msg.infos, new_client->nickname, INFOS_LEN - 1);
    send(connfd, &response_msg, sizeof(response_msg), 0);

    printf("Bienvenue sur le serveur, %s!\n", new_client->nickname);
}

// Fonction pour effectuer la liaison sur un port donné
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

// Fonction principale pour gérer la communication avec les clients
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
        const char* new_nickname = msgstruct.infos; 
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
        strncpy(target_nickname, msgstruct.infos, NICK_LEN - 1); 
        target_nickname[NICK_LEN-1] = '\0'; 
        recv(client->pfd.fd, private_message, msgstruct.pld_len, 0);
        private_message[msgstruct.pld_len] = '\0'; 
        printf("Private message from %s to %s: %s\n", msgstruct.nick_sender, target_nickname, private_message);
        handle_private_message(client, target_nickname, private_message);
    } else if (msgstruct.type == MULTICAST_CREATE) {
        handle_create_channel(client, msgstruct.infos);
    } else if (msgstruct.type == MULTICAST_LIST) {
        handle_channel_list_request(client);
    } else if (msgstruct.type == MULTICAST_QUIT) {
        handle_multicast_quit(client, msgstruct.infos);
    } else if (msgstruct.type == MULTICAST_JOIN) {
        handle_multicast_join(client, msgstruct.infos);
    } else if (msgstruct.type == MULTICAST_SEND) {
        char multicast_message[MSG_LEN];
        memset(multicast_message, 0, sizeof(multicast_message));

        bytes_received = recv(client->pfd.fd, multicast_message, msgstruct.pld_len, 0);
        if (bytes_received <= 0) {
            printf("Le client s'est déconnecté ou une erreur est survenue.\n");
            close(client->pfd.fd);
            remove_client(client);
        } else {
            for (ClientNode* tmp = head; tmp; tmp = tmp->next) {
                if (tmp != client && strcmp(tmp->channel_name, client->channel_name) == 0) {
                    struct message multicast_msg;
                    memset(&multicast_msg, 0, sizeof(multicast_msg));
                    multicast_msg.type = MULTICAST_SEND;
                    strncpy(multicast_msg.nick_sender, client->nickname, NICK_LEN - 1); 
                    strncpy(multicast_msg.infos, client->channel_name, CHANNEL_LEN - 1); 
                    multicast_msg.pld_len = strlen(multicast_message); 

                    send(tmp->pfd.fd, &multicast_msg, sizeof(multicast_msg), 0);
                    send(tmp->pfd.fd, multicast_message, multicast_msg.pld_len, 0);
                }
            }
        }
    } else if (msgstruct.type == FILE_REQUEST) {
        char file_path[MSG_LEN];
        recv(client->pfd.fd, file_path, msgstruct.pld_len, 0);
        file_path[msgstruct.pld_len] = '\0';
        handle_file_request(client, msgstruct.infos, file_path);
    } else if (msgstruct.type == FILE_ACCEPT || msgstruct.type == FILE_REJECT) {
        handle_file_response(client, msgstruct.type);
    } else {
            char received_msg[MSG_LEN];
        memset(received_msg, 0, sizeof(received_msg));

        bytes_received = recv(client->pfd.fd, received_msg, msgstruct.pld_len, 0);
        if (bytes_received <= 0) {
            printf("Le client s'est déconnecté ou une erreur est survenue.\n");
            close(client->pfd.fd);
            remove_client(client);
            return;
        }

        printf("pld_len: %i / nick_sender: %s / type: %s, infos: %s\n", msgstruct.pld_len, msgstruct.nick_sender, msg_type_str[msgstruct.type], msgstruct.infos);
        printf("Reçu: %s\n", received_msg);
    }
}

// Boucle de sondage principale du serveur
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

// Fonction principale du serveur
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Utilisatioon : %s <port_serveur>\n", argv[0]);
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