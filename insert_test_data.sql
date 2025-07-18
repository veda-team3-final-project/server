INSERT INTO lines (
  indexNum, x1, y1, x2, y2, name, mode, leftMatrixNum, rightMatrixNum
) VALUES (
  1, 663, 197, 858, 267, 'name1', 'Left', 2, 4
);
-- CCTV에선 4배한 2653, 791, 3433, 1071로 처리

INSERT INTO baseLines (
  indexNum, matrixNum1, x1, y1, matrixNum2, x2, y2
) VALUES (
  1, 2, 356, 453, 4, 3670, 1755
);

INSERT INTO baseLines (
  indexNum, matrixNum1, x1, y1, matrixNum2, x2, y2
) VALUES (
  2, 1, 3076, 157, 3, 1943, 1451
);

-- curl test문 (CCTV에 line 넣기 위해 필요) 

-- curl -X PUT --digest -u admin:admin123@ \
--   'https://192.168.0.137/opensdk/WiseAI/configuration/linecrossing' \
--   -H 'Accept: application/json' \
--   -H 'Content-Type: application/json' \
--   -H 'Cookie: TRACKID=0842ca6f0d90294ea7de995c40a4"aac6' \
--   -H 'Origin: https://192.168.0.137' \
--   -H 'Referer: https://192.168.0.137/home/setup/opensdk/html/WiseAI/index.html' \
--   --compressed \
--   --data-raw '{"channel":0,"enable":true,"line":[{"objectTypeFilter":["Person","Vehicle.Bicycle","Vehicle.Car","Vehicle.Motorcycle","Vehicle.Bus","Vehicle.Truck"],"name":"namsse1","index":1,"mode":"Right","lineCoordinates":[{"x":2653,"y":791},{"x":3433,"y":1071}]}]}'\
--   --insecure




