# 위클리 수정 검증 2026-11-04

- 데이터/영문 Times New Roman, 한글 맑은 고딕. 스핀 상태 S = (multiplicity − 1)/2 및 이탤릭 표시.
- Figure/Table SEQ 필드 전체 굵게 지정, 마침표 뒤 xml:space로 공백 보존.
- 위클리 에너지 표의 a/b/c 첨자와 설명 제거, Dispersion/Complexation 기본 제외.
- SI Generator 단계별 공통 원자 최대 spin 절댓값 순위 6개와 Rest/Sum 적용. 번호는 모델에 유지하고 표에는 원소기호만 표시.
- SI suggestGeometry(records, 6) 직접 재사용, 빈 geometry 표 생략. 셀 세로 여백 기본 0 pt, 고정 열 너비 적용.
- Gaussian route의 등호 주변 공백 차이가 BB 검증 실패를 일으키던 문제 수정. 재분석 시 과거 행 경고가 재사용되던 문제 수정.
- 날짜/프로젝트/버전별 모델·설정·PNG/GIF·Future work 저장. 이전 버전 유지, 오프라인 불러오기와 Word 재작성 지원.
- 렌더링 그룹 크기 변경 중 미생성 companion 접근 방지, camera API 변경 시 동기화 신호 전달.
- 한글 경로에서 MinGW 외부 소스 object 경로 오류를 피하도록 SI StructureLabels도 로컬 include adapter로 빌드.

검증: Release 전체 빌드, CTest 5/5 통과(core, docx_si_format, upgrade, render_controls, future_editor). GUI 및 단일 EXE --smoke-test 성공. 실제 저장 출력의 스핀 비교 4개 그룹 재분석 시 모든 선택 원자와 Rest/Sum이 수치로 출력되며 Sum은 약 0/2/4. 재분석에 서버 조회/다운로드는 수행하지 않음. Word PDF 변환으로 Times New Roman, 이탤릭, 굵은 번호, 캡션 공백 및 조밀한 표를 시각 점검. 저장본 모델/설정/그림으로 재사용 검증. 기존 EXE는 backups/Youngkly-before-weekly-revision-20261104-134053.exe에 보존.
