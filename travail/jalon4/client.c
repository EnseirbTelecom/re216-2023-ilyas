//client.c
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include "common.h"
#include "msg_struct.h"

#define MSG_LEN 1024

char current_channel[INFOS_LEN] = {'\0'};
char created_channel[INFOS_LEN] = {'\0'};

char pseudo[NICK_LEN] = {0};
bool hasNickname = false;

// Fonction pour envoyer un message complet au serveur
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

// Fonction pour recevoir un message complet du serveur
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

// Fonction pour générer l'invite de commande
char* generate_prompt(const char* nickname, const char* channel) {
    static char prompt[256];
    if (channel[0] != '\0') {
        snprintf(prompt, sizeof(prompt), "%% %s[%s]> ", nickname, channel);
    } else {
        snprintf(prompt, sizeof(prompt), "%% %s> ", nickname);
    }
    return prompt;
}

// Fonction pour établir la connexion avec le serveur
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


// Fonction pour gérer l'identification du client
void handle_identification(int sockfd) {
    struct message msgstruct;
    char buff[MSG_LEN];

    while (!hasNickname) {
        printf("Entrez votre pseudo avec la commande /nick : ");
        fgets(buff, MSG_LEN, stdin);

        size_t len = strlen(buff);
        if (len > 0 && buff[len - 1] == '\n') {
            buff[len - 1] = '\0';
            len--;
        }

        if (strncmp(buff, "/nick ", 6) == 0) {
            strncpy(pseudo, buff + 6, NICK_LEN - 1);
            hasNickname = true;

            msgstruct.pld_len = strlen(pseudo);
            strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
            msgstruct.type = NICKNAME_NEW;
            msgstruct.infos[0] = '\0';

            send(sockfd, &msgstruct, sizeof(msgstruct), 0);
        } else {
            printf("Commande invalide. Veuillez entrer votre pseudo avec la commande /nick : ");
        }
    }
}

// Fonction pour gérer la réponse du serveur à la commande /who
void handle_who_response(int sockfd) {
    struct message msgstruct;

    struct message who_request;
    who_request.type = NICKNAME_LIST;
    who_request.pld_len = 0;
    who_request.nick_sender[0] = '\0';
    who_request.infos[0] = '\0';

    send(sockfd, &who_request, sizeof(who_request), 0);

    if (recv(sockfd, &msgstruct, sizeof(msgstruct), 0) <= 0) {
        perror("Le serveur s'est déconnecté ou une erreur est survenue");
        return;
    }

    if (msgstruct.type == NICKNAME_LIST) {

        printf("[Server] : Les utilisateurs connectés sont:\n");
        char* user_list = msgstruct.infos;
        char* token = strtok(user_list, "\n");
        while (token != NULL) {
            printf("%s\n", token);
            token = strtok(NULL, "\n");
        }
    } else {
        printf("Réponse inattendue du serveur.\n");
    }
}


// Fonction pour envoyer une demande WHOIS au serveur
void handle_whois_request(int sockfd, const char* target_user) {
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = NICKNAME_INFOS;
    msg.pld_len = strlen(target_user);
    strncpy(msg.nick_sender, pseudo, NICK_LEN - 1);
    strncpy(msg.infos, target_user, INFOS_LEN - 1);
    ssize_t bytes_sent = send(sockfd, &msg, sizeof(struct message), 0);
    if (bytes_sent == -1) {
        perror("Failed to send whois request");
    }
}

// Fonction pour envoyer un message privé au serveur
void handle_private_message(int sockfd, const char* target, const char* message) {
    struct message msgstruct;
    memset(&msgstruct, 0, sizeof(struct message));
    msgstruct.pld_len = strlen(message);
    strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
    msgstruct.type = UNICAST_SEND;
    strncpy(msgstruct.infos, target, NICK_LEN - 1); 
    send(sockfd, &msgstruct, sizeof(msgstruct), 0);
    send(sockfd, message, msgstruct.pld_len, 0);
}

// Fonction pour envoyer une demande de création de salon au serveur
void send_multicast_create_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_CREATE;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);
    msg.infos[INFOS_LEN - 1] = '\0'; 

    if (send(sockfd, &msg, sizeof(struct message), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de création de salon");
    }
}

// Fonction pour envoyer une demande de quitter un salon au serveur
void send_multicast_quit_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_QUIT;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);

    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de quitter le salon");
    }
}

// Fonction pour envoyer une demande de rejoindre un salon au serveur
void send_multicast_join_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_JOIN;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);

    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de rejoindre le salon");
    }
}

// Fonction principale pour gérer la communication avec le serveur
void echo_client(int sockfd) {
    handle_identification(sockfd);
    struct pollfd fds[2];
    char buff[MSG_LEN];
    struct message msgstruct;

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
            if (!hasNickname) {
                handle_identification(sockfd);
            } else {
                printf("%s", generate_prompt(pseudo, current_channel));
                memset(buff, 0, MSG_LEN);
                fgets(buff, MSG_LEN, stdin);

                size_t len = strlen(buff);
                if (len > 0 && buff[len - 1] == '\n') {
                    buff[len - 1] = '\0';
                    len--;
                }

                if (strcmp(buff, "/quit") == 0) {
                    close(sockfd);
                    printf("Déconnecté du serveur.\n");
                    exit(EXIT_SUCCESS);
                } else if (strncmp(buff, "/nick ", 6) == 0) {
                    const char* newNickname = buff + 6; 
                    size_t newNicknameLen = strlen(newNickname);

                    if (newNicknameLen > 0) {
                        
                        struct message msgstruct;
                        msgstruct.pld_len = newNicknameLen;
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = NICKNAME_CHANGEMENT;
                        strncpy(msgstruct.infos, newNickname, INFOS_LEN - 1);

                        send(sockfd, &msgstruct, sizeof(struct message), 0);

                        strncpy(pseudo, newNickname, NICK_LEN - 1);
                    } else {
                        printf("Le nouveau pseudo ne peut pas être vide.\n");
                    }
                } else if (strcmp(buff, "/who") == 0) {
                    handle_who_response(sockfd);
                    continue;
                } else if (strncmp(buff, "/whois ", 7) == 0) {
                    const char* targetUser = buff + 7;
                    size_t targetUserLen = strlen(targetUser);

                    if (targetUserLen > 0) {
                        struct message msgstruct;
                        msgstruct.pld_len = targetUserLen;
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = NICKNAME_INFOS;
                        strncpy(msgstruct.infos, targetUser, INFOS_LEN - 1);

                        send(sockfd, &msgstruct, sizeof(struct message), 0);

                        continue; 
                    } else {
                        printf("Le pseudonyme cible pour la requête WHOIS ne peut pas être vide.\n");
                    } 
                    continue;
                } else if (strncmp(buff, "/msgall ", 8) == 0) {
                    const char* broadcastMessage = buff + 8; 

                    if (broadcastMessage[0] != '\0') {
                        struct message msgstruct;
                        msgstruct.pld_len = strlen(broadcastMessage);
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = BROADCAST_SEND;

                        strncpy(msgstruct.infos, broadcastMessage, INFOS_LEN - 1);

                        send(sockfd, &msgstruct, sizeof(struct message), 0);

                    } else {
                        printf("Usage : /msgall <message>\n");
                    }
                    continue;
                } else if (strncmp(buff, "/msg ", 5) == 0) {
                    const char* targetAndMessage = buff + 5;
                    char* target = strtok((char*)targetAndMessage, " ");
                    const char* message = strtok(NULL, "");

                    if (target != NULL && message != NULL) {
                        handle_private_message(sockfd, target, message);
                    } else {
                        printf("Usage : /msg <destinataire> <message>\n");
                    }
                    continue;
                } else if (strncmp(buff, "/create ", 8) == 0) {
                    const char* channel_name = buff + 8; 
                    if (channel_name[0] != '\0') {
                        send_multicast_create_message(sockfd, channel_name);
                    } else {
                        printf("Usage : /create <nom_du_salon>");
                    }
                } else if (strcmp(buff, "/channel_list") == 0) {
                    struct message msgstruct;
                    memset(&msgstruct, 0, sizeof(struct message));
                    msgstruct.type = MULTICAST_LIST;
                    msgstruct.pld_len = 0; 
                    strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1); 
                    msgstruct.infos[0] = '\0'; 

                    if (send(sockfd, &msgstruct, sizeof(msgstruct), 0) < sizeof(msgstruct)) {
                        perror("send()");
                    }
                    continue;
                } else if (strncmp(buff, "/quit ", 6) == 0) {
                    const char* channel_name = buff + 6; 
                    if (channel_name[0] != '\0') {
                        send_multicast_quit_message(sockfd, channel_name);
                        current_channel[0] = '\0'; 
                    } else {
                        printf("Usage : /quit <nom_du_salon>\n");
                    }
                } else if (strncmp(buff, "/join ", 6) == 0) {
                    const char* channel_name = buff + 6; 
                    if (channel_name[0] != '\0') {
                        send_multicast_join_message(sockfd, channel_name);
                        strncpy(current_channel, channel_name, INFOS_LEN - 1); 
                    } else {
                        printf("Usage : /join <nom_du_salon>\n");
                    }
                } else if (strncmp(buff, "/send ", 6) == 0) {
                    char* targetAndFilePath = buff + 6; 
                    char* target = strtok(targetAndFilePath, " ");
                    const char* filePath = strtok(NULL, "");

                    if (target != NULL && filePath != NULL) {
                        struct message msgstruct;
                        memset(&msgstruct, 0, sizeof(struct message));
                        msgstruct.pld_len = strlen(filePath);
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = FILE_REQUEST;
                        strncpy(msgstruct.infos, target, NICK_LEN - 1);
                        // Envoyer la structure du message
                        send(sockfd, &msgstruct, sizeof(msgstruct), 0);
                        // Envoyer le chemin du fichier
                        send(sockfd, filePath, msgstruct.pld_len, 0);
                    } else {
                        printf("Usage : /send <destinataire> <chemin_du_fichier>\n");
                    }
                    continue;
                } else {
                    if (strcmp(current_channel, "") != 0) {
                        struct message msgstruct;
                        memset(&msgstruct, 0, sizeof(struct message));
                        msgstruct.pld_len = strlen(buff);
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = MULTICAST_SEND;
                        strncpy(msgstruct.infos, current_channel, INFOS_LEN - 1); // Remplissez le champ infos avec le nom du salon
                        // Envoyer la structure du message
                        send(sockfd, &msgstruct, sizeof(msgstruct), 0);
                        // Envoyer le contenu du message
                        send(sockfd, buff, msgstruct.pld_len, 0);
                    } else {
                        struct message msgstruct;
                        memset(&msgstruct, 0, sizeof(struct message));
                        msgstruct.pld_len = strlen(buff);
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = ECHO_SEND;

                        // Envoyer la structure du message
                        send(sockfd, &msgstruct, sizeof(msgstruct), 0);
                        // Envoyer le contenu du message
                        send(sockfd, buff, msgstruct.pld_len, 0);
                    }
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            memset(buff, 0, MSG_LEN);
            memset(&msgstruct, 0, sizeof(struct message));

            if (recv(sockfd, &msgstruct, sizeof(struct message), 0) <= 0) {
                perror("Le serveur s'est déconnecté ou une erreur est survenue");
                exit(EXIT_FAILURE);
            }

            else if (hasNickname) {
                if(msgstruct.type == NICKNAME_NEW){
                    printf("Votre pseudonyme est désormais : %s\n", msgstruct.infos);
                } else if (msgstruct.type == NICKNAME_DOUBLON) {
                    printf("Le pseudonyme %s est déjà pris par un autre utilisateur.\n", msgstruct.nick_sender);
                    close(sockfd);
                    exit(EXIT_FAILURE);
                } else if(msgstruct.type == NICKNAME_CHANGEMENT){
                    printf("Votre pseudonyme est désormais : %s\n", msgstruct.infos);
                } else if (msgstruct.type == NICKNAME_INFOS) {
                    printf("%s\n", msgstruct.infos);
                } else if (msgstruct.type == BROADCAST_SEND) {
                    printf("[%s] : %s\n", msgstruct.nick_sender, msgstruct.infos); 
                } else if (msgstruct.type == UNICAST_SEND) {
                    char private_message[INFOS_LEN];
                    if (recv(sockfd, private_message, msgstruct.pld_len, 0) <= 0) {
                        perror("Erreur lors de la réception du message privé");
                        exit(EXIT_FAILURE);
                    }
                    private_message[msgstruct.pld_len] = '\0'; 
                    printf("[%s] : %s\n", msgstruct.nick_sender, private_message);
                } else if (msgstruct.type == MULTICAST_CREATE){
                    strncpy(current_channel, msgstruct.infos, INFOS_LEN - 1);
                    current_channel[INFOS_LEN - 1] = '\0';
                    printf("Salon '%s' créé avec succès. Vous avez été ajouté au salon.\n", current_channel);
                } else if (msgstruct.type == MULTICAST_CREATE_QUIT){
                    strncpy(current_channel, msgstruct.infos, INFOS_LEN - 1);
                    current_channel[INFOS_LEN - 1] = '\0';
                    printf("Salon '%s' créé avec succès. Vous avez été ajouté au salon. Votre ancien salon a été supprimé car il était vide.\n", current_channel);
                } else if (msgstruct.type == MULTICAST_CREATE_FAILED){
                    printf("%s\n", msgstruct.infos);
                } else if (hasNickname && msgstruct.type == MULTICAST_LIST) {
                    printf("%s\n", msgstruct.infos);
                } else if (msgstruct.type == MULTICAST_QUIT) {
                    if (strcmp(msgstruct.infos, "") != 0) {
                        printf("%s\n", msgstruct.infos); 
                    }
                } else if (msgstruct.type == MULTICAST_JOIN) {
                    if (strcmp(msgstruct.infos, "") != 0) {
                        printf("%s\n", msgstruct.infos); 
                    } 
                } else if (msgstruct.type == MULTICAST_NOTIFICATION) {
                    printf("[%s] : %s\n", msgstruct.nick_sender, msgstruct.infos);
                } else if (msgstruct.type == MULTICAST_SEND) {
                    char multicast_message[INFOS_LEN];
                    if (recv(sockfd, multicast_message, msgstruct.pld_len, 0) <= 0) {
                        perror("Erreur lors de la réception du message multicast");
                        exit(EXIT_FAILURE);
                    }
                    multicast_message[msgstruct.pld_len] = '\0'; 

                    printf(" %s> : %s\n", msgstruct.nick_sender, multicast_message);
                } else if (msgstruct.type == FILE_REQUEST) {
                    char file_path[MSG_LEN];
                    recv(sockfd, file_path, msgstruct.pld_len, 0);
                    file_path[msgstruct.pld_len] = '\0';

                    printf("%s veut vous envoyer le fichier '%s'. Acceptez-vous? [Y/N]\n", msgstruct.nick_sender, file_path);
                    char response;
                    scanf(" %c", &response);

                    struct message response_msg;
                    memset(&response_msg, 0, sizeof(struct message));
                    strncpy(response_msg.nick_sender, pseudo, NICK_LEN - 1);
                    response_msg.infos[0] = '\0';
                    response_msg.pld_len = 0;

                    if (response == 'Y' || response == 'y') {
                        response_msg.type = FILE_ACCEPT;
                        send(sockfd, &response_msg, sizeof(struct message), 0);
                        printf("Transfert de fichier accepté. En attente du démarrage du transfert...\n");
                    } else {
                        response_msg.type = FILE_REJECT;
                        send(sockfd, &response_msg, sizeof(struct message), 0);
                        printf("Transfert de fichier refusé.\n");
                    } 
                } else if (msgstruct.type == FILE_ACCEPT) {
                    printf("Votre demande de transfert de fichier vers %s a été acceptée.\n", msgstruct.nick_sender);
                } else if (msgstruct.type == FILE_REJECT) {
                    printf("Votre demande de transfert de fichier vers %s a été refusée.\n", msgstruct.nick_sender);
                }
            }
        }
    }
}

// Fonction principale du programme
int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Utilisation : %s <nom_serveur> <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd = handle_connect(argv[1], argv[2]);
    echo_client(sockfd);
    close(sockfd);
    return EXIT_SUCCESS;
}