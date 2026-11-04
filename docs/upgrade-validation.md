# Youngkly 0.2.0 구현 및 검증 기록

검증일: 2026-10-21 (Asia/Seoul). 모든 소스 변경과 검증 산출물은 Youngkly 폴더 안에 저장했다. Git 저장소와 적용할 AGENTS.md는 없었다. 기존 루트 `Youngkly.exe`, 원본 보고서와 기존 outputs 보고서는 교체하지 않았다. 새 실행 파일은 루트 `Youngkly-upgraded.exe`이다.

## 구현 내용

| 요구 | 구현 및 관련 파일 |
|---|---|
| 파일 읽기 | `src/FileTransfer.cpp`, `src/GaussianStore.*`, `src/Pipeline.cpp`: size/mtime 일괄 확인, 로컬 SHA-256 검증, 최대 3개 병렬 전송, 전송 전후 메타데이터 비교, QSaveFile 원자적 반영, 원본/BB 해시 및 SI parser 소스 버전에 따른 분석 캐시. 기존 서버 우선순위/중복/실패 의미 보존. |
| 동일 뷰 | `src/Alignment.*`, `src/AtomMapping.*`, `src/CameraApi.inc`, `src/RenderDialog.*`, `src/render_main.cpp`: TS 기준 proper rotation 정렬, 명시적 permutation 및 anchor 지원, 세 창 동기화, 공유 직교 카메라/배율, 정지 구조 및 진동 전체의 공통 crop, 그룹 뷰 저장. 원자 대응은 사용자가 확인해야 한다. |
| 논문형 표 | `src/Docx.cpp`, `report_layout`: 상하 0.75 pt/헤더 아래 0.5 pt, 문장 8 pt/블록 12 pt, 셀 상하 3 pt/좌우 4 pt, 긴 에너지 헤더에 가중 열 폭 적용, 반복 헤더/행 분할 금지, 본문 행 keepNext 연쇄 해제. |
| 분석 문장 | `src/Narrative.*`, `config/phrases.ko.json`: 단조/TS 국소/부호/작은 변화/동일 값 분류, 이면각 주기성, 음의 0 및 한국어 숫자 조사 처리. 연속된 같은 변화는 묶고 표·행·규칙·원시 값/관측 목록을 모델에 보존. 근거 없는 화학적 해석과 placeholder 제거. |
| Future work | `src/FutureWork.*`, `src/FutureEditor.*`, `config/future-work.json`: 날짜/연구/주제/출처/결과/질문/다음 행동/목적/상태/선택 일정/확정 여부를 저장. 확정된 현행 계획만 출력. 과거 계획 이월은 미확정 후보로 추가. |

`src/app_main.cpp`는 QProcess 비동기 흐름에 취소와 진행 기록을 추가하고 실행마다 별도 outputs 폴더를 사용한다. `src/Workspace.cpp`는 기존 spreadsheet ID별 다운로드 공유를 유지하며 시트/분석 타이밍과 Future work 검토 상태를 기록한다. `tools/package.ps1`은 별도 출력 경로를 받고 기존 실행 파일을 교체하지 않는다.

Youngcraft와 SI Generator의 외부 소스는 수정하지 않았다. 카메라 API는 CMake가 Youngkly 빌드 폴더에 생성하는 헤더 어댑터에만 추가한다. SI parser는 기존 소스를 링크하며 소스 변경이 감지되면 캐시 버전 해시도 갱신한다. C++20/Qt 런타임과 단일 EXE 배포 구조를 유지한다.

## 성능 측정

동일한 로컬 XLSX 스냅샷에서 선택한 Gaussian/BB 6개, 총 2,685,028 bytes를 사용했다. 변경 전 실행 파일은 보존된 `build/Youngkly.exe`, 변경 후는 `build-upgrade/Youngkly.exe`이다. 각 버전마다 서로 다른 비어 있는 클라이언트 캐시 3개를 만들고 각각 최초 실행 및 재실행을 측정했다. 표의 값은 3회 중앙값이다. 프로세스 시작, XLSX 해석, 검색/메타데이터, 전송 및 분석을 포함하며 렌더링/DOCX/단일 EXE의 최초 런타임 추출 시간은 제외한다. 서버 측 OS 캐시를 초기화한 실험은 아니다.

| 조건 | 변경 전 | 변경 후 | 시간 감소 |
|---|---:|---:|---:|
| 실제 lion SSH 서버, 클라이언트 캐시 없음 | 7.101초 | 5.768초 | 18.8% |
| 실제 lion SSH 서버, 변경 없는 재실행 | 6.773초 | 2.313초 | 65.9% |
| 같은 파일의 로컬 미러, 캐시 없음 | 0.737초 | 0.823초 | 캐시 생성 비용으로 최초 실행 증가 |
| 같은 파일의 로컬 미러, 재실행 | 0.630초 | 0.396초 | 37.2% |

새 버전 재실행 3회 모두 본문 다운로드 0회, Gaussian 재파싱 0회, 파일/파싱 캐시 적중 각 6회였다. 실제 서버 재실행은 검색 및 메타데이터 확인을 위한 SSH 2회였다. `--force-refresh`로 다시 받거나 분석할 수 있다. size/mtime은 원격 내용 동일성의 완전한 증명이 아니며, 로컬 SHA-256 검증은 캐시 손상을 확인하는 용도다. 원격 내용이 size/mtime을 그대로 유지한 채 바뀐 경우에는 강제 새로고침이 필요하다.

원시 측정과 단계별 지표: `outputs/upgrade-validation/remote-benchmark.json`, `benchmark.json` 및 각 benchmark 폴더의 로그.

## 데이터와 렌더링 검증

실제 저장된 데이터 32개, 17,008,628 bytes를 사용해 4개 경로를 검증했다. 모든 입력 SHA-256과 시트 셀, 분석 원본 좌표가 보존되었다. 총 12개 PNG, 4개 GIF의 전체 128프레임을 확인했다. 각 그룹의 카메라와 crop, PNG 크기가 동일하며, GIF 프레임은 공통 경계 안에 있다. 변환 행렬의 determinant는 +1이다. PNG는 600 dpi로 저장했다.

검증용 fixture 설정에서 기존 IRC 세 구조의 identity 대응을 명시적으로 확인한 것으로 설정했다. 이는 렌더러 검증 조건이며 실제 사용자 설정에 자동 확인 기본값을 넣지 않았다. 실제 작업에서는 같은 원소 순서만으로 원자 정체성을 확정하지 않는다. 필요하면 `atom_mapping`을 지정하고 확인해야 한다.

`outputs/combined-model.json`은 이전 에너지 표 스키마라 직접 조판 전후 기준으로 쓰면 잘못된 차이가 생긴다. 보존된 변경 전 core 라이브러리에 `tools/analyze_fixture.cpp`를 연결해 동일 원본을 다시 분석한 `baseline-model.json`과 비교했다. 에너지·geometry의 행/열/헤더/값 및 개별 원자 spin 값이 일치하며, 최종 DOCX의 모든 표 셀을 새 모델과 대조했다. 조판을 위해 열이나 수치를 추가하지 않았다.

별도의 정확성 수정: 전체 원자 spin 데이터가 불완전한 상태에서 합산하던 `Rest`/`Sum`은 이제 `N/A`다. 4개 경로의 해당 8행(24개 값)에 적용되었다. 상세 전후 값은 `artifact-checks.json`에 기록했다.

## 문서와 문장 확인

Word COM으로 DOCX를 PDF로 내보내고, bundled Python의 pypdfium2로 전체 페이지를 렌더링했다. 본 보고서 8쪽 및 과거 원본에 근거한 Future work 예시 2쪽을 모두 확인했다. 에너지 헤더의 잘못된 줄바꿈을 고쳤고 숫자 잘림, 겹침, 페이지 밖 넘침은 없었다. 문단의 노란색 검토 강조는 기존 방식대로 유지했다. 긴 블록은 페이지를 넘길 수 있다.

확정된 2026-10-21 계획이 제공되지 않아 본 보고서의 Future work에는 제목만 남겼다. 원본의 다음 주 계획을 현재 계획으로 옮겨 쓰지 않았다. 별도 예시는 원본 보고서의 날짜인 2026-10-14와 실제 계획 문장을 사용했다. 테스트용 계획은 사용자 `config/future-work.json`에 넣지 않았다.

분석 문장은 사용자 원본의 용어와 서술 방식을 따른 신규 초안이며, 원본에 직접적인 spin/geometry 표현 사례가 없어 사용자 확정 표현이라고 주장하지 않는다. 실제 값이 들어간 예:

- Co1의 spin density는 세 구조에서 모두 0.000으로 나타났습니다.
- Mn1의 spin density는 TS에서 3.960까지 감소하였으나, IRC forward에서는 4.775로 다시 증가하였습니다.
- H4−O53 거리는 IRC reverse, TS, IRC forward에서 각각 3.197 Å, 2.418 Å, 1.556 Å로 감소하였습니다.

표시 정밀도와 원시 값, 변화 규칙 및 source table row가 모델에 함께 남는다. 의미상 같은 원자가 대응하지 않는 경우 자동 문장은 보류한다. spin 값/거리만으로 산화 상태, radical, 결합 형성/절단을 확정하지 않는다.

## 실행한 검증

- `ctest --test-dir build-upgrade --output-on-failure`: core, upgrade, render_controls, future_editor 4/4 통과.
- 캐시 재사용, 단일 BB 변경/연관 분석 무효화, 로컬 캐시 손상 복구, force refresh, 새 중복 경로, 상위 서버 실패, 다른 연구 루트의 동명 파일, 한글/공백 경로.
- 강체 정렬 및 +1 determinant, 퇴화 anchor 거부, 명시적 atom permutation/중복 매핑 거부.
- 단조/TS 국소/부호 변화/0 부근 잡음/누락/음의 0/이면각 ±180° 및 각도 단위.
- 카메라 저장/복원 시 이미지 보존, 잘못된 카메라 상태 거부, 3개 뷰 동기화, 기존 고해상도/수소/조명 렌더 테스트.
- Future work 날짜/연구/확정 필터, placeholder 배제, 편집 창 추가/수정/다시 열기/삭제 및 과거 날짜 보존.
- `tools/verify_report.py`: 두 DOCX의 OOXML, 원본 보존 부분, 셀/폰트/간격/페이지 나눔/GIF 프레임 검증 통과.
- `tools/verify_upgrade_artifacts.py`: 입력/원본 좌표 보존, 모델/표 셀 일치, 공통 카메라/crop, 전체 GIF 프레임 경계 검증 통과.
- 단일 EXE `--smoke-test`: exit 0, 6개 연구 선택 UI와 Future work 편집/취소 버튼 표시 확인. 패키지 안의 CLI/앱/renderer SHA-256이 최종 빌드와 일치했다.

## 남은 제약

- 원자 수가 다른 구조의 자동 대응, 동일 원소 사이의 화학적 정체성 자동 추론은 하지 않는다. 사용자 매핑/anchor/확인이 필요하다.
- 모든 연구/서버/실제 계산을 새로 검증한 것은 아니다. 실제 성능 측정은 lion의 6개 파일이고, 서버 장애·다른 서버 fallback 의미는 로컬 회귀 fixture로 검증했다.
- 실행 중인 원격 계산을 강제로 바꾸거나 SSH 전송을 끊는 실험은 하지 않았다. 전송 전후 메타데이터 및 바이트 길이 검증과 QSaveFile을 사용하며, 손상된/불완전한 캐시는 회귀 테스트에서 다시 받아 복구했다.
- UI의 실제 사용자 회전 제스처부터 모든 연구 전체 보고서까지의 수동 연속 작업을 수행한 것은 아니다. 그룹 UI/동기화는 Qt 테스트로, 실제 데이터의 분석→렌더링→DOCX 흐름은 CLI로, 패키지는 UI 시작 smoke test로 확인했다.
- 시각 검증의 PDF에는 GIF 첫 프레임만 표시된다. GIF 자체의 32프레임 디코딩과 경계는 별도로 검사했다.

실행: 루트 `Youngkly-upgraded.exe`. 결과: `outputs/run-...`. 검증 보고서: `outputs/upgrade-validation/Youngkly-upgraded-report.docx` 및 같은 이름의 PDF. 테스트용 과거 계획 예시: `Future-work-reference-example.docx`.
