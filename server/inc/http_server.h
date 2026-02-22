#pragma once

#include "HttpServer.h"

void start_http_server(int port = 8080);
void init_new_http_server(gw::HttpServer* server);

