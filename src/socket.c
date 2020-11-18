#include <stdio.h>
#include "socket.h"
#include "global.h"

#define PORT 3333

static const char *TAG = "socket";

void do_retransmit(const int sock);

/**
 * Task reponsável por fazer a conexão do SocketTCP.
 */
void tcp_server_task(void *pvParameters) {
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    } else if (addr_family == AF_INET6) {
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    #if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        // Note that by default IPV6 binds to both protocols, it is must be disabled
        // if both protocols used at the same time (used in CI)
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
    #endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    loop {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        if (source_addr.sin6_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }
}

/**
 * Função que captura os valores recebidos por uma transmissão socket e 
 * devolve os dados do histórico de leituras no formato JSON.
 */
void do_retransmit(const int sock) {
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            if (strcmp(rx_buffer, "GET") == 0) {
                int element;
                char jsonObjects[300] = "";
                char json[500] = "";
                for (int i = 0; i < DATA_BUFFER_SIZE; i++) {
                    xQueueReceive(bufferLuminosityNotification, &element, 0);
                    int messagesWaiting = uxQueueMessagesWaiting(bufferLuminosityNotification);
                    char buf[20] = "";
                    if (messagesWaiting < 1) {
                        sprintf(buf, "{\"value\":%d}", element);
                        strcat(jsonObjects, buf);
                        break;
                    } else {
                        sprintf(buf, "{\"value\":%d},", element);
                        strcat(jsonObjects, buf);
                    }
                    ESP_LOGI(TAG, "%s", jsonObjects);
                }
                sprintf(json, "[%s]", jsonObjects);
                ESP_LOGI(TAG, "%s", json);
                int jsonLen = strlen(json);
                int to_write = jsonLen;
                while (to_write > 0) {
                    int written = send(sock, json + (jsonLen - to_write), to_write, 0);
                    if (written < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    }
                    to_write -= written;
                }
            }
        }
    } while (len > 0);
}