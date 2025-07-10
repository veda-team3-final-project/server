// g++ -o db_management db_management.cpp -l SQLiteCpp -l sqlite3 -std=c++17
#include "db_management.hpp"

void create_table_detections(SQLite::Database& db)
{
    db.exec("CREATE TABLE IF NOT EXISTS detections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "image BLOB, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL)");
    cout << "'detections' 테이블이 준비되었습니다.\n";
    return;
}

void insert_data_detections(SQLite::Database& db, vector<unsigned char> image, string timestamp) {
    try {
        // SQL 인젝션 방지를 위해 Prepared Statement 사용
        SQLite::Statement query(db, "INSERT INTO detections (image, timestamp) VALUES (?, ?)");
        query.bind(1, image.data(), image.size());
        query.bind(2, timestamp);
        cout << "Prepared SQL for insert: " << query.getExpandedSQL() << endl;
        query.exec();
        
        cout << "데이터 추가: (시간: " << timestamp << ")" << endl;
    } catch (const exception& e) {
        // 이름이 중복될 경우 (UNIQUE 제약 조건 위반) 오류가 발생할 수 있습니다.
        cerr << "데이터 '" << timestamp << "' 추가 실패: " << e.what() << endl;
    }
    return;
}

vector<Detection> select_data_for_timestamp_range_detections(SQLite::Database& db, string startTimestamp, string endTimestamp){
    vector<Detection> detections;
    try {
        SQLite::Statement query(db, "SELECT * FROM detections WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp");
        query.bind(1, startTimestamp);
        query.bind(2, endTimestamp);
        cout << "Prepared SQL for select data vector: " << query.getExpandedSQL() << endl;
        while (query.executeStep()) {

            const unsigned char* ucharBlobData = static_cast<const unsigned char*>(query.getColumn("image").getBlob());
            int blobSize = query.getColumn("image").getBytes();
            vector<unsigned char> image(ucharBlobData,ucharBlobData+blobSize);

            string timestamp = query.getColumn("timestamp");

            Detection detection = {image,timestamp};
            detections.push_back(detection);
        }
    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return detections;
}

void delete_all_data_detections(SQLite::Database& db) {
    try {
        SQLite::Statement query(db, "DELETE FROM detections");
        cout << "Prepared SQL for delete all: " << query.getExpandedSQL() << endl;
        int changes = query.exec();
        cout << "테이블의 모든 데이터를 삭제했습니다. 삭제된 행 수: " << changes << endl;
    } catch (const exception& e) {
        cerr << "테이블 전체 삭제 실패: " << e.what() << endl;
    }
    return;
}

///////////////////////////////////////////////

// void create_table_lines(SQLite::Database& db){
//     db.exec("CREATE TABLE IF NOT EXISTS lines ("
//         "name TEXT PRIMARY KEY NOT NULL, "
//         "x1 INTEGER NOT NULL , "
//         "y1 INTEGER NOT NULL , "
//         "x2 INTEGER NOT NULL , "
//         "y2 INTEGER NOT NULL , "
//         "mode TEXT NOT NULL)"); // mode = "Right", "Left", "BothDirection"
//     cout << "'lines' 테이블이 준비되었습니다.\n";
//     return;
// }

// void insert_data_lines(SQLite::Database& db, vector<unsigned char> image, string timestamp) {

// }

// vector<unsigned char> select_all_data_lines(SQLite::Database& db){
//     vector<unsigned char> lineDatas;
//     try {
//         SQLite::Statement query(db, "SELECT * FROM lines ORDER BY index");
//         cout << "Prepared SQL for select data vector: " << query.getExpandedSQL() << endl;
//         while (query.executeStep()) {

//             int index = query.getColumn("index").getInt();
//             int x1 = query.getColumn("x1").getInt();
//             int y1 = query.getColumn("y1").getInt();
//             int x2 = query.getColumn("x2").getInt();
//             int y2 = query.getColumn("y2").getInt();
//             string mode = query.getColumn("mode");

//             logDatas.insert(logDatas.end(),string("10/1/").begin(),string("10/1/").end());
//             logDatas.insert(logDatas.end(),image.begin(),image.end());
//             logDatas.insert(logDatas.end(),string("/").begin(),string("/").end());
//             logDatas.insert(logDatas.end(),timestamp.begin(),timestamp.end());
//         }
//     } catch (const exception& e) {
//         cerr << "사용자 조회 실패: " << e.what() << endl;
//     }
//     return logDatas;
// }

// void delete_data_lines(SQLite::Database& db){

// }

// void delete_all_data_lines(SQLite::Database& db){

// }