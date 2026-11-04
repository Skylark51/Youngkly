# 스핀 최적화 구조 위클리 포함 수정

검증일: 2026-11-02 (Asia/Seoul).

선택한 날짜의 최적화 출력은 읽었지만 Word 본문은 IRC 세 구조만 처리해 파일명만 남던 문제를 수정했다. 시트의 동일 구간에서 다중도 접두사만 다른 `N-구조명-opt-freq.out` 또는 `.log`를 스핀 비교 그룹으로 만들고, 한 상태·두 상태·세 상태 이상을 지원한다. 시트 순서와 에너지 기준은 유지한다. 구조 PNG, 편집 가능한 에너지·스핀·geometry 표와 상태별 수치 문장을 출력한다. 스핀 비교에는 TS 진동 GIF나 활성화 장벽 문장을 쓰지 않는다.

BB의 원자 순서와 원소가 같을 때 회전·이동 정렬 후 최대 좌표 오차 0.002 Å로 검증한다. 기존 축별 좌표 비교가 단순 좌표 방향 변경을 다른 구조로 취급하던 문제도 수정했다. 실제 변형은 계속 제외한다. 외부 SI/Youngcraft 소스는 수정하지 않았다. 현재 Youngcraft와의 카메라 함수 이름 충돌, NBO parser 링크 의존성과 한글 경로 windres 빌드도 Youngkly 내에서 해결했다.

## 검증

- `tools/build.ps1 -BuildDirectory build-spin`: core, upgrade, render_controls, future_editor 4/4 통과.
- 1·2·3·4 상태의 매핑·문장, 단일/복수 상태 뷰 전환, 중복 다중도 보류, 회전된 BB 허용·변형된 BB 거부를 검사했다.
- 2026-11-05 기존 실행의 시트 스냅샷과 이미 다운로드한 파일을 재사용해 스핀 5그룹을 확인했다. 경로가 확정된 4그룹 12개 출력은 SI 에너지 성분 대조가 모두 matched였다.
- `tools/verify_spin_report.py`: 구조 그림 12개, 편집 가능한 표 12개, 상태 헤더, TS/GIF 배제와 보류 그룹 표시를 모델과 대조했다.
- 표준 `render_docx.py`는 설치된 LibreOffice가 없어 실행되지 않았다. 기존 Word COM 경로로 PDF를 내보내고 bundled pypdfium2로 8쪽을 렌더링해 확인했다. 문서 스타일은 기존 위클리 형식을 유지했다.
- 기존 IRC fixture 한 경로도 다시 렌더링했다. PNG 3개와 32프레임 TS GIF 생성·디코딩 검증이 통과했다.
- 단일 EXE 패키지를 루트 `Youngkly.exe`에 반영하고 SHA-256 일치 및 패키지 `--smoke-test` 성공을 확인했다. 이전 파일은 `Youngkly-before-spin-fix-20261102.exe`로 보존했다.

검증용 모델/문서/이미지/설정은 `outputs/spin-validation`에 있다. 이 설정의 `atom_mapping_confirmed`는 렌더러 검증용 조건이며 사용자 설정으로 옮기지 않았다. 실제 보고서에서는 기존처럼 사용자가 구조 뷰와 원자 대응을 확인해야 한다.

## 남은 데이터 확인

`1/3/5-MnIII-12MTP-MeCN-endon-peroxo-opt-freq.out`는 lion의 연구 폴더와 `.local/state/nrun`에 각각 존재한다. 어느 파일을 사용할지 임의로 결정하지 않으며 보고서에 경로 확인이 필요하다고 표시한다. 전체 원자 spin이 없는 출력의 Rest/Sum은 기존대로 N/A를 유지한다. 검증 보고서는 사용자 원자 대응 확인을 대신하는 최종 보고서가 아니다.
