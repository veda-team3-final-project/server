// g++ -o db_management db_management.cpp -l SQLiteCpp -l sqlite3 -std=c++17
#include "db_management.hpp"

void create_table(SQLite::Database& db)
{
    db.exec("CREATE TABLE IF NOT EXISTS detections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "image BLOB, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL)");
    cout << "'detection' 테이블이 준비되었습니다.\n";
    return;
}

void insert_data(SQLite::Database& db, vector<unsigned char> image, string timestamp) {
    try {
        // SQL 인젝션 방지를 위해 Prepared Statement 사용
        SQLite::Statement query(db, "INSERT INTO detections (image, timestamp) VALUES (?, ?)");
        query.bind(1, image.data(), image.size());
        query.bind(2, timestamp);
        query.exec();
        
        cout << "데이터 추가: (시간: " << timestamp << ")" << endl;
    } catch (const exception& e) {
        // 이름이 중복될 경우 (UNIQUE 제약 조건 위반) 오류가 발생할 수 있습니다.
        cerr << "데이터 '" << timestamp << "' 추가 실패: " << e.what() << endl;
    }
    return;
}

// 모든 데이터 출력용 select
void select_all_data(SQLite::Database& db) {
    cout << "\n--- 모든 데이터 목록 ---" << endl;
    try {
        SQLite::Statement query(db, "SELECT * FROM detections ORDER BY timestamp");
        while (query.executeStep()) {
            int id = query.getColumn("id");

            const unsigned char* ucharBlobData = static_cast<const unsigned char*>(query.getColumn("image").getBlob());
            int blobSize = query.getColumn("image").getBytes();
            vector<unsigned char> image(ucharBlobData,ucharBlobData+blobSize);

            string timestamp = query.getColumn("timestamp");
            cout << "ID: " << id  << ", 타임스탬프: " << timestamp << endl;
            for(int i=0;i<image.size();i++){
                cout << image[i];
            }
            cout << "\n";
        }
    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return;
}

LogData select_data_for_timestamp(SQLite::Database& db, string timestamp){
    try {
        SQLite::Statement query(db, "SELECT * FROM detections WHERE timestamp = ? ORDER BY timestamp");
        query.bind(1, timestamp);
        while (query.executeStep()) {

            const unsigned char* ucharBlobData = static_cast<const unsigned char*>(query.getColumn("image").getBlob());
            int blobSize = query.getColumn("image").getBytes();
            vector<unsigned char> image(ucharBlobData,ucharBlobData+blobSize);

            string timestamp = query.getColumn("timestamp");

            LogData logData = {image, timestamp};
            return logData;
        }
    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return;
}

void delete_data(SQLite::Database& db, int id) {
    SQLite::Statement query(db, "DELETE FROM detections WHERE id = ?");
    query.bind(1, id);
    int changes = query.exec();
    if (changes > 0) {
        cout << id << "해당 id의 data를 삭제했습니다." << endl;
    } else {
        cout << id << "삭제하지 못했습니다." << endl;
    }
    return;
}

void delete_all_data(SQLite::Database& db) {
    try {
        SQLite::Statement query(db, "DELETE FROM detections");
        int changes = query.exec();
        cout << "테이블의 모든 데이터를 삭제했습니다. 삭제된 행 수: " << changes << endl;
    } catch (const exception& e) {
        cerr << "테이블 전체 삭제 실패: " << e.what() << endl;
    }
    return;
}