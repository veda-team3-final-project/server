-- detections(감지 이미지&텍스트 테이블)을 제외한 모두 테이블 delete sql문

SELECT '--- Current Data ---';

select * from lines;
select * from baseLines;
select * from verticalLineEquations;

delete from lines;
delete from baseLines;
delete from verticalLineEquations;

SELECT 'Reset Complete';