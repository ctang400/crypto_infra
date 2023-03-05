#include <string.h>

#include <unistd.h>

#include <arpa/inet.h>

#include <openssl/ssl.h>
#include <iostream>
#include <core/link/IPv4Address.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <core/utils/llhttp/llhttp.h>

int on_status(llhttp_t* parser){
  // std::cout<<"status:"<<std::string(at, length)<<" "<<parser->status_code<<std::endl;

  
  return HPE_PAUSED;
}

 int on_body(llhttp_t* parser, const char*at, size_t length){
  std::cout<<"body:"<<std::string(at, length)<<" "<<parser->status_code<<std::endl;
  
  return 0;
}

int main (int argc, char *argv[])

{

    int s,  result;

    struct sockaddr_in srv_addr;

    char buf[1<<21];

    SSL_CTX *ctx;

    SSL *ssl;

    llhttp_t parser;
llhttp_settings_t settings;

/* Initialize user callbacks and settings */
llhttp_settings_init(&settings);

/* Set user callback */
settings.on_body= on_body;
settings.on_message_complete = on_status;

/* Initialize the parser in HTTP_BOTH mode, meaning that it will select between
 * HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
 * input.
 */
llhttp_init(&parser, HTTP_RESPONSE, &settings);

/* Parse request! */

    /* Create a TLS client context with a CA certificate */

    ctx = SSL_CTX_new(TLS_client_method());

    //SSL_CTX_use_certificate_file(ctx, "/etc/ssl/certs/ca-certificates.crt", SSL_FILETYPE_PEM);

 

    /* Set the address and port of the server to connect to */

    srv_addr.sin_family = AF_INET;

    srv_addr.sin_port = 443;
    const std::string host = "testnet.binance.vision";
    auto addr = IPv4Address(host.c_str(), 443);
    //auto addr = IPv4Address("www.baidu.com", 443);

    inet_pton(AF_INET, "3.33.174.87", &srv_addr.sin_addr);

 

    /* Create a socket and SSL session */

    s = socket(AF_INET, SOCK_STREAM, 0);

    ssl = SSL_new(ctx);

    SSL_set_fd(ssl, s);

 

    /* Try to connect */
    struct sockaddr_in saddr = addr.toSockAddrIn();
    //auto p = addr.peer();
    //char str[60];
    //inet_ntop(AF_INET, &p, , sizeof(str));
    //std::cout << str << std::endl;
    SSL_set_tlsext_host_name(ssl, host.c_str());
    result = connect(s, (struct sockaddr *)&saddr, sizeof(saddr));
    std::cout << result << std::endl;
    if (result == 0)
    {

      /* Run the OpenSSL handshake */

      result = SSL_connect(ssl);
      std::cout << result << std::endl;
      std::cout << ERR_error_string(ERR_get_error(), NULL) << std::endl;
      std::cout << SSL_get_version(ssl) << std::endl;
      /* Exchange some data if the connection succeeded */

      if (result == 1)
      {

        sprintf(buf, "GET /api/v3/pin HTTP/1.1\r\nHost: testnet.binance.vision\r\nConnection: close\r\n\r\n");

        SSL_write(ssl, buf, strlen(buf) + 1);
        SSL_read(ssl, buf, sizeof(buf));
        int s = 5;
        parser.data = &s;
        llhttp_reset(&parser);
        llhttp_execute(&parser, (char *)buf, 5);
        //llhttp_resume(&parser);
        llhttp_execute(&parser, (char *)buf + 5, 1000);
        std::cout << *(int*)parser.data << std::endl;
        llhttp_finish(&parser);
        //std::cout << std::string(buf, 5) << std::endl;
        //printf("Received message from server: ‘%s'\n", buf);
      }
    }

    /* Done */

    close(s);

    SSL_free(ssl);

    SSL_CTX_free(ctx);

    return 0;

}