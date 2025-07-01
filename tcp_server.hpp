// TCP 서버 구동 모듈

#ifndef TCPSERVER_HPP
#define TCPSERVER_HPP

#include <iostream>   // 표준 입출력 (std::cout, std::cerr)
#include <string>     // 문자열 처리 (std::string)
#include <vector>     // 동적 배열 (std::vector, 여기서는 사용되지 않지만 이전 컨텍스트에서 포함됨)

// POSIX 소켓 API 관련 헤더
#include <sys/socket.h> // socket, bind, listen, accept
#include <netinet/in.h> // sockaddr_in
#include <unistd.h>     // close
#include <arpa/inet.h>  // inet_ntoa

// 오류 처리를 위한 추가 헤더
#include <cstring>    // memset, strerror
#include <cerrno>     // errno

int tcp_run();

#endif // TCPSERVER_HPP