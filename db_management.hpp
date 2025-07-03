// SQLite DB 관리 모듈.

#pragma once
// SQLiteC++ 외부 라이브러리
#include <SQLiteCpp/SQLiteCpp.h>

#include <iostream>   // 표준 입출력 (std::cout, std::cerr)
#include <string>     // 문자열 처리 (std::string)
#include <vector>     // 동적 배열 (std::vector, 여기서는 사용되지 않지만 이전 컨텍스트에서 포함됨)
#include <fstream>    // 이미지 파일 테스트용

using namespace std;

void create_table(SQLite::Database& db);

void insert_data(SQLite::Database& db, vector<unsigned char> image, string timestamp);

void select_all_data(SQLite::Database& db);

void delete_data(SQLite::Database& db, int id);

void delete_all_data(SQLite::Database& db);