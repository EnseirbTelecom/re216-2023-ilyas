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

// Pseudo du client
char pseudo[NICK_LEN] = {0};
bool hasNickname = false;

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

// Fonction pour envoyer un message au serveur
void send_message(int sockfd, struct message* msg, const char* content) {
    send(sockfd, msg, sizeof(struct message), 0);
    send(sockfd, content, msg->pld_len, 0);
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
            printf("Votre pseudonyme est désormais : %s\n", pseudo);

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
    printf("Message privé envoyé à %s : %s\n", target, message);
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
                printf("Message: ");
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
                    // La commande commence par "/nick ", extrayez le nouveau pseudo
                    const char* newNickname = buff + 6; // Pointe après "/nick "
                    size_t newNicknameLen = strlen(newNickname);

                    if (newNicknameLen > 0) {
                        // Envoi du nouveau pseudo au serveur
                        struct message msgstruct;
                        msgstruct.pld_len = newNicknameLen;
                        strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                        msgstruct.type = NICKNAME_CHANGEMENT;
                        strncpy(msgstruct.infos, newNickname, INFOS_LEN - 1);

                        send_message(sockfd, &msgstruct, newNickname);
                        printf("Votre pseudonyme est désormais : %s\n", newNickname);
                        strncpy(pseudo, newNickname, NICK_LEN - 1);
                    } else {
                        printf("Le nouveau pseudo ne peut pas être vide.\n");
                    }
                    continue;
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
                        send_message(sockfd, &msgstruct, NULL); // Pas besoin d'envoyer de contenu supplémentaire, juste la structure

                        continue; // Pour passer au prochain tour de boucle et éviter de traiter d'autres logiques.
                    } else {
                        printf("Le pseudonyme cible pour la requête WHOIS ne peut pas être vide.\n");
                    }
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
                        send_message(sockfd, &msgstruct, NULL);
                        printf("Message diffusé à tous les utilisateurs : %s\n", broadcastMessage);
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
                }
                else {
                    struct message msgstruct;
                    msgstruct.pld_len = len;
                    strncpy(msgstruct.nick_sender, pseudo, NICK_LEN - 1);
                    msgstruct.type = ECHO_SEND;
                    msgstruct.infos[0] = '\0';

                    // Envoyer la structure du message et son contenu
                    send_message(sockfd, &msgstruct, buff);
                    printf("Message envoyé !\n");
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
                if (msgstruct.type == NICKNAME_DOUBLON) {
                    printf("Le pseudonyme %s est déjà pris par un autre utilisateur.\n", msgstruct.nick_sender);
                    close(sockfd);
                    exit(EXIT_FAILURE);
                } else if (msgstruct.type == ECHO_SEND) {
                    // Afficher les informations de longueur et le contenu du message uniquement pour ECHO_SEND
                    if (recv(sockfd, buff, msgstruct.pld_len, 0) <= 0) {
                        perror("Erreur lors de la réception du message");
                        exit(EXIT_FAILURE);
                    }
                    printf("pld_len: %i / nick_sender: %s / type: %s, infos %s:\n", msgstruct.pld_len, msgstruct.nick_sender, msg_type_str[msgstruct.type], msgstruct.infos);
                    printf("Reçu: %s\n", buff);
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
                    printf("Message privé de %s : %s\n", msgstruct.nick_sender, private_message);
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
