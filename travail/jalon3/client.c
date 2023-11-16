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

// Pseudo du client
char pseudo[NICK_LEN] = {0};
bool hasNickname = false;

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

    // Résolution de l'adresse du serveur
    if (getaddrinfo(server_name, server_port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }

    // Parcourir les résultats possibles pour trouver une connexion réussie
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) break;
        close(sockfd);
    }

    // Vérifier si une connexion réussie a été trouvée
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

            // Envoi du pseudo au serveur
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

    // Envoyer la commande /who au serveur
    struct message who_request;
    who_request.type = NICKNAME_LIST;
    who_request.pld_len = 0;
    who_request.nick_sender[0] = '\0';
    who_request.infos[0] = '\0';

    send(sockfd, &who_request, sizeof(who_request), 0);

    // Attendre la réponse du serveur
    if (recv(sockfd, &msgstruct, sizeof(msgstruct), 0) <= 0) {
        perror("Le serveur s'est déconnecté ou une erreur est survenue");
        return;
    }

    if (msgstruct.type == NICKNAME_LIST) {
        // Afficher la liste des utilisateurs connectés en colonne
        printf("[Server] : Les utilisateurs connectés sont:\n");
        char* user_list = msgstruct.infos;
        char* token = strtok(user_list, "\n"); // Séparer la liste en lignes
        while (token != NULL) {
            printf("%s\n", token);
            token = strtok(NULL, "\n"); // Passage à la ligne suivante
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
    strncpy(msgstruct.infos, target, NICK_LEN - 1); // Stockez le destinataire dans le champ "infos"
    // Envoyer la structure du message
    send(sockfd, &msgstruct, sizeof(msgstruct), 0);
    // Envoyer le contenu du message
    send(sockfd, message, msgstruct.pld_len, 0);
}

void send_multicast_create_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_CREATE;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);
    msg.infos[INFOS_LEN - 1] = '\0'; // Assurez-vous que la chaîne est terminée correctement

    // Envoyer la demande de création de salon au serveur
    if (send(sockfd, &msg, sizeof(struct message), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de création de salon");
    }
}

void send_multicast_quit_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_QUIT;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);

    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de quitter le salon");
    }
}

void send_multicast_join_message(int sockfd, const char* channel_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = MULTICAST_JOIN;
    strncpy(msg.infos, channel_name, INFOS_LEN - 1);

    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur lors de l'envoi de la demande de rejoindre le salon");
    }
}

void send_file_request(int sockfd, const char* receiver, const char* file_name) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    msg.type = FILE_REQUEST;
    strncpy(msg.nick_sender, pseudo, NICK_LEN - 1);
    strncpy(msg.infos, receiver, INFOS_LEN - 1);
    send(sockfd, &msg, sizeof(msg), 0);
    send(sockfd, file_name, strlen(file_name), 0); // Envoyer le nom du fichier en tant que payload
}

void handle_file_request_received(int sockfd, struct message* msg) {
    printf("%s wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]\n", msg->nick_sender, msg->infos);
    char response[10];
    fgets(response, 10, stdin);

    struct message response_msg;
    memset(&response_msg, 0, sizeof(response_msg));
    strncpy(response_msg.nick_sender, pseudo, NICK_LEN - 1);

    if (response[0] == 'Y' || response[0] == 'y') {-
        response_msg.type = FILE_ACCEPT;
        // Ajouter l'adresse IP et le port pour la connexion P2P ici
        snprintf(response_msg.infos, INFOS_LEN - 1, "%s:%d", "127.0.0.1", 8081); // Exemple d'adresse IP et de port
    } else {
        response_msg.type = FILE_REJECT;
    }

    send(sockfd, &response_msg, sizeof(response_msg), 0);
}

void handle_file_response_received(struct message* msg) {
    if (msg->type == FILE_ACCEPT) {
        printf("%s accepted file transfer. Connecting to %s and sending the file...\n", msg->nick_sender, msg->infos);
        // Code pour établir la connexion P2P et envoyer le fichier
    } else if (msg->type == FILE_REJECT) {
        printf("%s cancelled file transfer.\n", msg->nick_sender);
    }
}

void send_file_ack(int sockfd, const char* file_name) {
    struct message ack_msg;
    memset(&ack_msg, 0, sizeof(ack_msg));
    ack_msg.type = FILE_ACK;
    strncpy(ack_msg.nick_sender, pseudo, NICK_LEN - 1);
    strncpy(ack_msg.infos, file_name, INFOS_LEN - 1);

    send(sockfd, &ack_msg, sizeof(ack_msg), 0);
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
                    const char* newNickname = buff + 6; // Pointe après "/nick "
                    size_t newNicknameLen = strlen(newNickname);

                    if (newNicknameLen > 0) {
                        // Envoi du nouveau pseudo au serveur
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
                    const char* targetUser = buff + 7; // Pointe après "/whois "
                    size_t targetUserLen = strlen(targetUser);

                    if (targetUserLen > 0) {
                        // Créez la structure de message pour la requête WHOIS
                        struct message msgstruct;
                        msgstruct.pld_len = targetUserLen;
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = NICKNAME_INFOS;
                        strncpy(msgstruct.infos, targetUser, INFOS_LEN - 1);

                        // Envoyer la demande WHOIS au serveur
                        send(sockfd, &msgstruct, sizeof(struct message), 0);

                        continue; // Pour passer au prochain tour de boucle et éviter de traiter d'autres logiques.
                    } else {
                        printf("Le pseudonyme cible pour la requête WHOIS ne peut pas être vide.\n");
                    } 
                    continue;
                } else if (strncmp(buff, "/msgall ", 8) == 0) {
                    const char* broadcastMessage = buff + 8; // Pointe après "/msgall "

                    if (broadcastMessage[0] != '\0') {
                        // Composez le message pour la diffusion
                        struct message msgstruct;
                        msgstruct.pld_len = strlen(broadcastMessage);
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = BROADCAST_SEND;

                        // Copiez le contenu du message dans le champ "infos"
                        strncpy(msgstruct.infos, broadcastMessage, INFOS_LEN - 1);

                        // Envoyer le message au serveur pour diffusion
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
                    const char* channel_name = buff + 8; // Pointe après "/create "
                    if (channel_name[0] != '\0') {
                        send_multicast_create_message(sockfd, channel_name);
                    } else {
                        printf("Usage : /create <nom_du_salon>");
                    }
                } else if (strcmp(buff, "/channel_list") == 0) {
                    struct message msgstruct;
                    memset(&msgstruct, 0, sizeof(struct message));
                    msgstruct.type = MULTICAST_LIST;
                    msgstruct.pld_len = 0; // Pas de longueur de charge utile, car nous n'envoyons aucune donnée
                    strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1); // Définissez nick_sender sur le pseudo du client
                    msgstruct.infos[0] = '\0'; // Le champ 'infos' devrait être une chaîne vide comme spécifié

                    // Envoyez la demande de liste des salons au serveur
                    if (send(sockfd, &msgstruct, sizeof(msgstruct), 0) < sizeof(msgstruct)) {
                        perror("send()");
                    }
                    continue;
                } else if (strncmp(buff, "/quit ", 6) == 0) {
                    const char* channel_name = buff + 6; // Extrait le nom du salon
                    if (channel_name[0] != '\0') {
                        send_multicast_quit_message(sockfd, channel_name);
                        current_channel[0] = '\0'; // Réinitialiser le canal actuel
                    } else {
                        printf("Usage : /quit <nom_du_salon>\n");
                    }
                } else if (strncmp(buff, "/join ", 6) == 0) {
                    const char* channel_name = buff + 6; // Extrait le nom du salon
                    if (channel_name[0] != '\0') {
                        send_multicast_join_message(sockfd, channel_name);
                        strncpy(current_channel, channel_name, INFOS_LEN - 1); // Mettre à jour le salon actuel
                    } else {
                        printf("Usage : /join <nom_du_salon>\n");
                    }
                } else if (strncmp(buff, "/send ", 6) == 0) {
                    char* receiver = strtok(buff + 6, " ");
                    char* file_name = strtok(NULL, "");
                    send_file_request(sockfd, receiver, file_name);
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
                    printf("[%s] : %s\n", msgstruct.nick_sender, msgstruct.infos); // Afficher uniquement le message de diffusion ici
                } else if (msgstruct.type == UNICAST_SEND) {
                    char private_message[INFOS_LEN];
                    if (recv(sockfd, private_message, msgstruct.pld_len, 0) <= 0) {
                        perror("Erreur lors de la réception du message privé");
                        exit(EXIT_FAILURE);
                    }
                    private_message[msgstruct.pld_len] = '\0'; // Assurez-vous que le message est correctement terminé
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
                        printf("%s\n", msgstruct.infos); // Affiche le message d'erreur si le salon n'est pas correct
                    }
                } else if (msgstruct.type == MULTICAST_JOIN) {
                    if (strcmp(msgstruct.infos, "") != 0) {
                        printf("%s\n", msgstruct.infos); // Affiche un message d'erreur si le salon n'est pas correct
                    } 
                } else if (msgstruct.type == MULTICAST_NOTIFICATION) {
                    printf("[%s] : %s\n", msgstruct.nick_sender, msgstruct.infos);
                } else if (msgstruct.type == MULTICAST_SEND) {
                    // Recevoir le contenu du message multicast
                    char multicast_message[INFOS_LEN];
                    if (recv(sockfd, multicast_message, msgstruct.pld_len, 0) <= 0) {
                        perror("Erreur lors de la réception du message multicast");
                        exit(EXIT_FAILURE);
                    }
                    multicast_message[msgstruct.pld_len] = '\0'; // Assurez-vous que le message est correctement terminé

                    // Afficher le message multicast
                    printf(" %s> : %s\n", msgstruct.nick_sender, multicast_message);
                } else if (msgstruct.type == FILE_REQUEST) {
                    printf("%s souhaite vous envoyer le fichier : %s. Acceptez-vous ? [Y/N]\n", msgstruct.nick_sender, msgstruct.infos);
                    char response[10];
                    fgets(response, 10, stdin);

                    struct message response_msg;
                    memset(&response_msg, 0, sizeof(response_msg));
                    strncpy(response_msg.nick_sender, msgstruct.nick_sender, NICK_LEN - 1);

                    if (response[0] == 'Y' || response[0] == 'y') {
                        response_msg.type = FILE_ACCEPT;
                    } else {
                        response_msg.type = FILE_REJECT;
                    }

                    send(sockfd, &response_msg, sizeof(response_msg), 0);
                } else if (msgstruct.type == FILE_ACCEPT) {
                    printf("%s a accepté le transfert du fichier.\n", msgstruct.nick_sender);
                    receive_file(sockfd, "nom_du_fichier_à_recevoir");
                } else if (msgstruct.type == FILE_REJECT) {
                    printf("%s a refusé le transfert du fichier.\n", msgstruct.nick_sender);
                } else if (msgstruct.type == FILE_ACK) {
                    printf("Accusé de réception du fichier reçu de %s : %s\n", msgstruct.nick_sender, msgstruct.infos);
                } else if (msgstruct.type == FILE_REQUEST) {
                    handle_file_request_received(sockfd, &msgstruct);
                } else if (msgstruct.type == FILE_ACCEPT || msgstruct.type == FILE_REJECT) {
                    handle_file_response_received(&msgstruct);
                }
            }
        }
    }
}

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