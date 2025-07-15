// g++ -o db_management db_management.cpp -l SQLiteCpp -l sqlite3 -std=c++17
#include "db_management.hpp"

///////////////////////////////////////////////
// Detections 테이블

void create_table_detections(SQLite::Database& db)
{
    db.exec("CREATE TABLE IF NOT EXISTS detections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "image BLOB, "
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL)");
    cout << "'detections' 테이블이 준비되었습니다.\n";
    return;
}

bool insert_data_detections(SQLite::Database& db, vector<unsigned char> image, string timestamp) {
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
        return false;
    }
    return true;
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
// Lines 테이블

void create_table_lines(SQLite::Database& db){
    db.exec("CREATE TABLE IF NOT EXISTS lines ("
        "indexNum INTEGER PRIMARY KEY NOT NULL, "
        "x1 INTEGER NOT NULL , "
        "y1 INTEGER NOT NULL , "
        "x2 INTEGER NOT NULL , "
        "y2 INTEGER NOT NULL , "
        "name TEXT NOT NULL UNIQUE , "
        "mode TEXT ," // mode = "Right", "Left", "BothDirections"
        "leftMatrixNum INTEGER, "
        "rightMatrixNum INTEGER)"); 
    cout << "'lines' 테이블이 준비되었습니다.\n";
    return;
}

bool insert_data_lines(SQLite::Database& db, int indexNum ,int x1, int y1, int x2, int y2, string name, string mode, int leftMatrixNum, int rightMatrixNum) {
    try {
        // SQL 인젝션 방지를 위해 Prepared Statement 사용
        SQLite::Statement query(db, "INSERT INTO lines (indexNum, x1, y1, x2, y2, name, mode, leftMatrixNum, rightMatrixNum) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        query.bind(1, indexNum);
        query.bind(2, x1);
        query.bind(3, y1);
        query.bind(4, x2);
        query.bind(5, y2);
        query.bind(6, name);
        query.bind(7, mode);
        query.bind(8, leftMatrixNum);
        query.bind(9, rightMatrixNum);
        cout << "Prepared SQL for insert: " << query.getExpandedSQL() << endl;
        query.exec();
        
        cout << "데이터 추가: (인덱스: " << indexNum << ")" << endl;
    } catch (const exception& e) {
        // 이름이 중복될 경우 (UNIQUE 제약 조건 위반) 오류가 발생할 수 있습니다.
        cerr << "데이터 '" << name << "' 추가 실패: " << e.what() << endl;
        return false;
    }
    return true;
}

vector<CrossLine> select_all_data_lines(SQLite::Database& db){
    vector<CrossLine> lines;
    try {
        SQLite::Statement query(db, "SELECT * FROM lines ORDER BY name");
        cout << "Prepared SQL for select data vector: " << query.getExpandedSQL() << endl;
        while (query.executeStep()) {

            int indexNum = query.getColumn("indexNum").getInt();
            int x1 = query.getColumn("x1").getInt();
            int y1 = query.getColumn("y1").getInt();
            int x2 = query.getColumn("x2").getInt();
            int y2 = query.getColumn("y2").getInt();
            string name = query.getColumn("name");
            string mode = query.getColumn("mode");
            int leftMatrixNum = query.getColumn("leftMatrixNum").getInt();
            int rightMatrixNum = query.getColumn("rightMatrixNum").getInt();


            CrossLine line = {indexNum, x1, y1, x2, y2, name, mode, leftMatrixNum, rightMatrixNum};
            lines.push_back(line);
        }
    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return lines;
}

bool delete_data_lines(SQLite::Database& db, int indexNum){
    try {
        SQLite::Statement query(db, "DELETE FROM lines WHERE indexNum = ?");
        query.bind(1,indexNum);

        cout << "Prepared SQL for delete one: " << query.getExpandedSQL() << endl;
        int changes = query.exec();
        cout << "테이블의 특정 데이터를 삭제했습니다. 삭제된 행 수: " << changes << endl;
        if(changes == 0){
            return false;
        }
    } catch (const exception& e) {
        cerr << "테이블 특정 데이터 삭제 실패: " << e.what() << endl;
        return false;
    }
    return true;
}

bool delete_all_data_lines(SQLite::Database& db){
    try {
        SQLite::Statement query(db, "DELETE FROM lines");
        cout << "Prepared SQL for delete all: " << query.getExpandedSQL() << endl;
        int changes = query.exec();
        cout << "테이블의 모든 데이터를 삭제했습니다. 삭제된 행 수: " << changes << endl;
        if(changes == 0){
            return false;
        }
    } catch (const exception& e) {
        cerr << "테이블 전체 삭제 실패: " << e.what() << endl;
        return false;
    }
    return true;
}

///////////////////////////////////////////////
// BaseLineCoordinates 테이블

void create_table_baseLineCoordinates(SQLite::Database& db)
{
    db.exec("CREATE TABLE IF NOT EXISTS baseLineCoordinates ("
        "matrixNum INTEGER PRIMARY KEY, "
        "x INTEGER NOT NULL, "
        "y INTEGER NOT NULL)");
    cout << "'BaseLineCoordinates' 테이블이 준비되었습니다.\n";
    return;
}

vector<BaseLineCoordinate> select_all_data_baseLineCoordinates(SQLite::Database& db){
    vector<BaseLineCoordinate> baseLineCoordinates;
    try {
        SQLite::Statement query(db, "SELECT * FROM baseLineCoordinates");
        cout << "Prepared SQL for select data vector: " << query.getExpandedSQL() << endl;
        while (query.executeStep()) {

            int matrixNum = query.getColumn("matrixNum").getInt();
            int x = query.getColumn("x").getInt();
            int y = query.getColumn("y").getInt();

            BaseLineCoordinate baseLineCoordinate = {matrixNum, x, y};
            baseLineCoordinates.push_back(baseLineCoordinate);
        }
    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return baseLineCoordinates;
}

bool insert_data_baseLineCoordinates(SQLite::Database& db,int matrixNum, int x,int y ){
    try {
        // SQL 인젝션 방지를 위해 Prepared Statement 사용
        SQLite::Statement query(db, "INSERT INTO baseLineCoordinates (matrixNum, x, y) VALUES (?, ?, ?)");
        query.bind(1, matrixNum);
        query.bind(2, x);
        query.bind(3, y);
        cout << "Prepared SQL for insert: " << query.getExpandedSQL() << endl;
        query.exec();
        
        cout << "데이터 추가: (매트릭스 인덱스: " << matrixNum << ")" << endl;
    } catch (const exception& e) {
        // 이름이 중복될 경우 (UNIQUE 제약 조건 위반) 오류가 발생할 수 있습니다.
        cerr << "데이터 '" << matrixNum << "' 추가 실패: " << e.what() << endl;
        return false;
    }
    return true;
}

///////////////////////////////////////////////
// VerticalLineEquation 테이블

void create_table_verticalLineEquation(SQLite::Database& db)
{
    db.exec("CREATE TABLE IF NOT EXISTS verticalLineEquations ("
        "indexNum INTEGER PRIMARY KEY, "
        "a REAL NOT NULL, "
        "b REAL NOT NULL)");
    cout << "'BaseLineCoordinates' 테이블이 준비되었습니다.\n";
    return;
}

VerticalLineEquation select_data_verticalLineEquation(SQLite::Database& db, int index){
    VerticalLineEquation verticalLineEquation;
    try {
        SQLite::Statement query(db, "SELECT * FROM verticalLineEquations WHERE index = ?");
        cout << "Prepared SQL for select data vector: " << query.getExpandedSQL() << endl;
        query.exec();

        int indexNum = query.getColumn("indexNum").getInt();
        double x = query.getColumn("x");
        double y = query.getColumn("y");

        verticalLineEquation = {indexNum, x, y};

    } catch (const exception& e) {
        cerr << "사용자 조회 실패: " << e.what() << endl;
    }
    return verticalLineEquation;
}

bool insert_data_verticalLineEquation(SQLite::Database& db, int index, double a, double b){
    try {
        // SQL 인젝션 방지를 위해 Prepared Statement 사용
        SQLite::Statement query(db, "INSERT INTO verticalLineEquations (indexNum, a, b) VALUES (?, ?, ?)");
        query.bind(1, index);
        query.bind(2, a);
        query.bind(3, b);
        cout << "Prepared SQL for insert: " << query.getExpandedSQL() << endl;
        query.exec();
        
        cout << "데이터 추가: (인덱스: " << index << ")" << endl;
    } catch (const exception& e) {
        // 이름이 중복될 경우 (UNIQUE 제약 조건 위반) 오류가 발생할 수 있습니다.
        cerr << "데이터 '" << index << "' 추가 실패: " << e.what() << endl;
        return false;
    }
    return true;
}