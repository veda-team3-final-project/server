// SQLite DB detections 테이블 관리 모듈

#pragma once
// SQLiteC++ 외부 라이브러리
#include <SQLiteCpp/SQLiteCpp.h>

#include <iostream>   // 표준 입출력 (std::cout, std::cerr)
#include <string>     // 문자열 처리 (std::string)
#include <vector>     // 동적 배열 (std::vector, 여기서는 사용되지 않지만 이전 컨텍스트에서 포함됨)
#include <fstream>    // 이미지 파일 테스트용

// json 처리를 위한 외부 헤더파일
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

struct Detection{
    vector<unsigned char> imageBlob;
    string timestamp;
};

struct CrossLine{
    string name;
    int x1;
    int y1;
    int x2;
    int y2;
    string mode;
    int leftMatrixNum;
    int rightMatrixNum;
};

void create_table_detections(SQLite::Database& db);

bool insert_data_detections(SQLite::Database& db, vector<unsigned char> image, string timestamp);

vector<Detection> select_data_for_timestamp_range_detections(SQLite::Database& db, string startTimestamp, string endTimestamp);

void delete_all_data_detections(SQLite::Database& db);

void create_table_lines(SQLite::Database& db);

bool insert_data_lines(SQLite::Database& db, string name, int x1, int y1, int x2, int y2);

vector<CrossLine> select_all_data_lines(SQLite::Database& db);

bool delete_data_lines(SQLite::Database& db, string name);

void delete_all_data_lines(SQLite::Database& db);