# Youngkly

Google Sheets의 I열과 J열 보고 날짜를 모두 확인하여 어느 한 열이라도 선택한 날짜와 일치하는 계산을 선택하고, 서버의 Gaussian 출력과 Youngcraft 그림을 모아 기존 위클리 형식의 Word 초안을 생성하는 C++20 프로그램입니다. 두 열이 모두 일치해도 같은 행은 한 번만 포함합니다.

## Windows 실행 파일

프로젝트 루트의 **`Youngkly.exe` 하나만 배포·실행**합니다. GUI, CLI, renderer, Qt 런타임과 기본 설정·보고서 예시가 단일 EXE에 포함됩니다. 처음 실행할 때 런타임을 `%LOCALAPPDATA%/Youngkly`에 자동으로 펼칩니다. 보고 날짜와 연구를 선택하고 `시트 날짜 확인` 또는 `Word 보고서 작성`을 누릅니다. 결과는 `outputs/run-날짜-시간` 폴더에 새로 저장됩니다. 보고서 작성 중 경로별로 공통 anchor atom IDs를 지정하고 세 구조를 함께 확인합니다. 가운데 TS에서 오른쪽 드래그로 회전, 가운데 드래그로 이동, 휠로 확대/축소한 후 원자 대응을 확인하고 `그룹 전체 적용 → 다음 경로`를 누릅니다. 세 구조는 공유 카메라와 공통 자르기 범위를 사용합니다.

실행 직후 기존 앱 아이콘과 시작 안내를 담은 스플래시 창을 표시합니다. 첫 실행의 런타임 준비 중에도 표시되며, 메인 앱이 준비되고 최소 1.4초가 지나면 0.24초 동안 살짝 아래로 이동하며 사라집니다. 작업표시줄에는 별도의 시작 창 항목을 만들지 않습니다.

첫 화면 왼쪽에는 보고 날짜 달력이 항상 표시되며, 달력 위의 “날먹하고싶다” 문구는 가운데 정렬됩니다. 오른쪽 연구 목록의 `모두 선택` / `모두 해제`로 보고서에 포함할 연구를 한 번에 변경할 수 있습니다.

뷰 창에는 C–H 수소 숨김, C–H activation, 밝기, 그림자 강도, TS 진동 미리보기 조절이 있습니다. 기본은 C에 붙은 수소만 숨기며 O–H·N–H 수소는 표시합니다. C–H activation을 선택하면 수소를 유지합니다. TS GIF에도 사용자가 선택한 방향과 수소 표시가 적용됩니다.

분자 그림은 Youngcraft의 **Youngcraft Studio** 프리셋을 공통으로 사용합니다. 흰 배경에 순수 빨강(O: RGB 255, 0, 0)·순수 파랑(N: RGB 0, 0, 255)·보라(Co), 검정에 가까운 탄소(C: RGB 12, 12, 12)를 사용합니다. 초록 계열은 파스텔 민트(F: RGB 145, 220, 175; Cl: RGB 155, 220, 165)로 표시합니다. Chemcraft 실제 출력물을 참고해 탄소 구슬은 작게, 질소·산소는 상대적으로 크게 표시합니다. 기본 원자 크기는 0.28, 원소별 배율은 C 0.75·N 1.08·O 0.98이며 결합 막대 반지름은 0.032입니다. 한 방향의 흰 반사점과 그 아래 넓고 부드러운 광택을 공용 셰이더에서 함께 표현합니다. 기본 밝기는 135, 그림자 강도는 24%이며 밝기는 70–180 범위에서 조절합니다.

정지 PNG는 화면 캡처를 확대하지 않고 기본 긴 변 3000px에서 새로 렌더링한 뒤 여백을 자릅니다. 600 dpi 정보를 기록하며, 2배 슈퍼샘플링·MSAA와 출력 크기에 맞춘 곡면 분할을 적용합니다. `render_image_long_edge_px`로 정지 그림, `render_animation_long_edge_px`로 GIF의 렌더링 크기(기본 1280px)를 조절합니다. TS의 고해상도 정지 PNG도 GIF와 함께 저장합니다. 두 해상도의 잘라낼 범위는 애니메이션 전체 움직임을 기준으로 맞춥니다. 보고서에는 Word 자동 그림 압축 금지 설정을 넣습니다. 공용 고해상도 렌더링 API는 Youngcraft의 그림 저장·복사·프레임 저장에도 적용됩니다.

Word의 한글은 맑은 고딕, 본문은 양쪽 정렬, 모든 표 값은 가운데 정렬입니다. 표와 본문 사이는 기본 8 pt, 표 블록 사이는 12 pt로 관리하며 Future work는 새 페이지에서 시작합니다. 표는 상하 0.75 pt, 헤더 아래 0.5 pt의 3선 표이고, 행 전체를 한 페이지에 강제로 묶지 않습니다. 스핀 표 아래에는 검증된 수치 변화 문장을 붙입니다. 원자 대응이 확인되지 않은 경우 분석 문장은 보류합니다.

Qt DLL, 이미지·네트워크 플러그인, CLI와 Youngcraft renderer를 EXE 안에 함께 묶었습니다. 별도 Qt·Python·CMake 설치 없이 실행합니다. EXE만 다른 빈 폴더로 옮겨도 기본 설정과 보고서 예시를 초기화합니다. 기존 사용자 설정은 덮어쓰지 않습니다.

추가 문서는 다음 이름으로 연결했습니다. InnoCORE 문서의 실제 탭 이름은 `정박사님`, `최박사님`입니다. 다른 문서는 각 연구 아래에 원래 탭 순서로 연결했습니다. 현재 전체 6개 연구, 5개 Google 문서, 21개 탭입니다.

| 연구 | 프로젝트 ID |
|---|---|
| Aldehyde deformylation (김효경 학생) | `aldehyde` |
| InnoCORE (정동현 박사) | `innocore-jdh` |
| InnoCORE (최지수 박사) | `innocore-cjs` |
| {FeNO}⁶ (전영진 학생) | `feno6` |
| Co(III)-peroxo photocleavage (정준 학생) | `co-peroxo-photo` |
| {CoNO}⁷ (전영진 학생) | `cono7` |

숫자 6과 7은 Word에서 `w:vertAlign=superscript`를 가진 실제 위첨자 run으로 작성합니다. 날짜가 없는 탭은 결과를 임의로 넣지 않습니다. 같은 행 번호가 여러 시트에 있어도 연구와 탭별로 분리하여 처리합니다.

## 현재 실행 가능한 흐름

1. 지정한 Google 문서를 XLSX로 읽습니다. 날짜 서식을 가진 Excel serial date와 `10/21` 텍스트 날짜를 구분하며, 다른 숫자는 날짜로 취급하지 않습니다.
2. 시트의 행 순서를 유지합니다. 같은 stem의 IRC reverse → TS → IRC forward 또는 IRC forward → TS → IRC reverse가 연속한 경우 자동으로 세 구조를 묶고 원래 순서를 유지합니다. 입력 파일 행은 별도의 작성 예정 항목으로 남깁니다.
   스핀 최적화 출력도 포함합니다. `1-구조명-opt-freq.out`, `3-구조명-opt-freq.out`, `5-구조명-opt-freq.out`처럼 앞의 다중도만 다른 출력은 같은 시트·구간 안에서 비교 그룹으로 묶습니다(`.log`도 지원). 선택한 날짜의 한 상태 또는 두 상태만 있어도 출력하며, 상태 순서는 시트를 따릅니다. 구조 그림과 에너지·스핀·geometry 표를 작성하고 TS GIF나 활성화 장벽 문장은 생성하지 않습니다. 중복 다중도나 중복 서버 경로는 보고서에 확인 사유를 남깁니다. BB 좌표는 원자 순서와 원소를 확인하고 proper rotation·이동 정렬 후 비교하므로 Gaussian의 좌표 방향 변경 때문에 정상 BB가 제외되지 않습니다.
3. lion → dumbo → tachyon 순서로 정확한 파일명을 검색합니다. size/mtime과 로컬 SHA-256을 확인하여 변경 없는 출력은 재다운로드하지 않으며, 전송은 기본 최대 3개씩 병렬 처리합니다. 파싱 결과는 원본/BB 내용 해시와 SI parser 버전으로 재사용합니다. 이미 발견한 파일은 다음 서버에서 재검색하지 않습니다. `-freq-bb.out`도 독립적으로 탐색하므로 원본과 다른 서버에 있어도 연결할 수 있습니다. 중복 경로는 임의로 선택하지 않으며, 서버 접속 실패를 파일 부재로 취급하지 않습니다.
4. SI Generator의 실제 C++ Gaussian parser와 energy/geometry 함수를 링크합니다. 최적화 좌표·열 보정은 frequency 출력에서, TZVPP electronic energy와 spin은 검증된 BB 출력에서 읽습니다. BB의 원자 순서와 좌표도 비교합니다.
5. 오른쪽 에너지 값과 수식을 함께 보관합니다. Excel 공유 수식을 행별 참조식으로 복원하고, 시트의 에너지 성분을 SI 계산 결과 및 성분 합계와 대조합니다. 기준 상태가 다른 표를 임의로 원점 변경하지 않습니다. 표는 SI Generator의 `ΔE`, `ΔΔE`, `E Total`, `ΔZ0`, `ΔΔE Thermal`, `−TΔS`, `ΔG` 열 순서, 아래/위첨자와 보정항 `+` 부호를 따릅니다. `E Total`과 `ΔG`는 굵게 표시합니다. Complexation 입력이 별도로 없으면 SI의 기본값 0을 사용합니다.
6. Youngcraft의 실제 OpenGL renderer로 그림을 만듭니다. TS를 기준으로 대응 원자의 proper rotation/병진 정렬을 하고 직교 투영·확대율·pan을 공유합니다. TS 진동 벡터에도 같은 회전을 적용합니다. 모든 정지 구조와 32개 프레임의 합집합으로 자릅니다. 세 이미지는 각각 너비 5 cm입니다. TS의 유일한 imaginary mode로 32프레임 GIF를 생성하고, 매 프레임 결합을 다시 계산합니다.
7. 상대 에너지, SI Generator 기준의 Mulliken spin density, key geometry를 **편집 가능한 Word 표**로 넣습니다. 변화에 대한 검토 표시는 문장에 적용합니다.
8. 예시 문서의 제목 1·2·3과 본문 paragraph/run 속성을 이용하여 DOCX를 만듭니다. 목차는 Word에서 갱신되도록 설정합니다.

프로그램 런타임은 C++/Qt입니다. Python은 개발 시 DOCX/GIF 검증에만 사용합니다.

## 실행

저장소 폴더에서 PowerShell 7로 실행합니다.

```powershell
.\tools\build.ps1
.\tools\run.ps1 -Date 2026-10-21
```

한 연구만 처리하려면 `-Project innocore-jdh`처럼 지정합니다. 단일 EXE를 다시 패키징할 때는 빌드 후 `tools/package.ps1`을 실행합니다.

결과는 `outputs/주간_연구_보고서_이영섭_10월_4주차_261021.docx`에 생성됩니다. 날짜는 반드시 지정합니다. 오늘 날짜를 보고 날짜로 추정하지 않습니다.

`-Week 4`로 주차를 직접 지정할 수 있습니다. 기본은 월요일 시작 달력 주차이며, 예시의 2026-10-14 = 10월 3주차와 일치합니다.

`-Offline`은 **시트 재다운로드만 생략**하고 저장된 XLSX를 이용합니다. 출력 파일 확인에는 서버에 접속합니다.

단계별 실행:

```powershell
$env:PATH = 'D:/yc-tools/msys64/ucrt64/bin;' + $env:PATH
.\build\Youngkly.exe --date 2026-10-21 --fetch --analyze
.\build\youngkly_render.exe outputs/report-model.json config/youngkly.json outputs/assets
.\build\Youngkly.exe --model outputs/report-model.json --docx 'outputs/주간_연구_보고서_이영섭_10월_4주차_261021.docx'
```

`--fetch` 없이 `--date`만 지정하면 저장된 시트에서 선택 계획 JSON만 생성합니다. `--resolve`는 탐색·다운로드까지, `--analyze`는 SI 분석까지 수행합니다.

## 설정 및 문장 뼈대

- `config/youngkly.json`: 시트 이름, 서버 탐색 순서·루트, 이미지 너비, 변화 기준, 프로젝트 제목, 원본 DOCX 경로.
- `config/phrases.ko.json`: 사용자 원본 문체에 맞춘 신규 초안 템플릿입니다. 원본에 직접적인 spin/geometry 표현 사례는 없어 사용자 확정 표현이라고 간주하지 않습니다. 단조 변화, TS 국소 변화, 부호 변화, 작은 변화와 동일 값을 구분합니다. 관측 사실만 서술하며 반복되는 같은 방향의 변화를 묶습니다.
- `narrative_energy_column`: `N`이면 TZVPP electronic energy, `S`이면 Gibbs free energy를 문장에 사용합니다. 현재는 `N`입니다. 상대 에너지 표에는 둘 다 표시합니다. barrier와 생성물 상대 에너지는 첫 구조를 기준으로 계산합니다.
- `reaction_rules.<section>.c_h_activation`: `true`이면 C에 붙은 H를 유지합니다. `false` 또는 미등록이면 C–H 수소만 기본으로 숨깁니다. 뷰 선택 창에서 직접 변경할 수 있습니다. 반응 종류를 파일명으로 확정하지 않습니다.
- `interactive_rendering`: 기본값 `true`. 자동 검증·배치 작업은 renderer 명령 끝에 `--batch`를 지정합니다.
- 뷰 확정 후 캡처의 흰 여백을 자동으로 자릅니다. 분자·그림자 경계에 긴 변의 1.5%(최소 4px)만 남기며, TS는 정지 이미지와 전체 32프레임의 합집합 범위를 동일하게 적용하여 움직임이 잘리거나 화면이 흔들리지 않도록 합니다.
- `include_dispersion`, `include_complexation`: SI Generator와 같이 상대 표의 해당 보정 열을 포함할지 선택합니다. 기본값은 모두 `false`이며 GUI의 체크박스로 필요한 경우에만 포함합니다.
- `reaction_rules.<section>.geometry_atom_groups`: 선택적으로 `[[1,2],[2,1,3],[4,1,2,3]]`처럼 1-based atom ID를 지정합니다. 각각 SI의 거리·각·이면각 함수를 사용합니다. 미지정 시 크게 변하는 가까운 원자쌍을 후보로 제안합니다.
- 기본 변화 기준: 거리 0.15 Å, 각도 5°, spin 0.2. 이 기준은 반응을 판정하는 물리 법칙이 아니라 검토 후보 선택 기준입니다.

## 초기 버전의 범위

최초 연결한 `gid=601555075`는 XLSX의 `3. Hydroperoxo Formation` 탭입니다. Google의 gid는 XLSX sheetId와 같지 않으므로 실제 탭 이름으로 매핑합니다. 2026-10-21 데이터에서 Aldehyde의 7개 행과 InnoCORE (정동현 박사)의 18개 행을 읽고 총 4개 경로의 분석·렌더링을 검증했습니다.

입력 파일이 있다는 이유로 계산 진행 상태나 연구 결론을 만들어내지 않습니다. 원소 순서 일치는 원자 대응의 후보일 뿐입니다. 원자 순서가 다른 경우 `reaction_rules.<section>.atom_mapping`에 시트의 세 구조 순서대로 TS 원래 atom ID → 각 구조의 원래 1-based atom ID 배열을 넣습니다. TS 배열은 항등 순서여야 합니다. 배치 렌더링에는 `atom_mapping_confirmed: true` 또는 같은 입력에 대해 이전에 확인하고 저장한 그룹 뷰가 필요합니다. 원자 수가 다르면 자동 정렬을 보류합니다.

새 반응이 자동 triplet 조건에 맞지 않거나 중복 경로가 있으면 `outputs/report-model.json`에 상태를 남깁니다. 묶이지 않은 출력 파일도 DOCX에 결과 확인 예정 문장으로 남깁니다. 계산이 끝났더라도 순서나 stem이 다른 경로를 자동으로 재배열하지 않습니다.

## 개발 구성

- `src/Sheet.*`: OOXML 시트·날짜·공유 수식·반응 경로.
- `src/Pipeline.*`: 서버 탐색, 출처·SHA-256, SI adapter, 표와 검토 문장.
- `src/render_main.cpp`: Youngcraft batch rendering 및 GIF 검증.
- `src/Docx.*`: 원본 보고서 component를 이용한 OOXML 보고서 생성.
- `src/Archive.*`: XLSX/DOCX ZIP 읽기·쓰기.
- `tests/core_tests.cpp`: 날짜, 수식 참조, 경로 묶기, 우선 탐색, BB fallback, 중복 파일 등의 회귀 검증.

기존 SI/Youngcraft 소스 위치는 CMake의 `SI_SOURCE_DIR`, `YOUNGCRAFT_SOURCE_DIR`로 지정합니다. 빌드 도구 기본 경로는 `D:/yc-tools`이며 `tools/build.ps1 -ToolRoot ...`로 바꿀 수 있습니다. Youngkly의 GIF adapter에는 검증 중 확인한 Youngcraft LZW code-width 전환의 1-code 시점 차이를 보정하는 패치가 있습니다.

SSH는 기존 key 파일 경로만 참조합니다. 실제 연결 대상과 사용자명은 기존 CMD shortcut에서 찾아 설정했습니다. 서버에 쓰거나 계산을 제출하는 동작은 없습니다.


## 업그레이드 사용법

- `Future work 편집`에서 보고 날짜와 연구에 맞는 계획을 입력합니다. 결과·질문·다음 행동·목적 칸은 완성된 문장으로 작성합니다. 출처와 다음 행동이 있고 확정한 항목만 보고서에 들어갑니다. 파일은 `config/future-work.json`이며 서버 접속 없이 편집합니다. 과거 계획은 이월 후보로 복사할 수 있고, 확인 전에는 출력하지 않습니다.
- `reaction_rules.<section>.alignment_anchor_atom_ids`로 공통 골격의 1-based TS 원자 번호를 지정할 수 있습니다. 최소 3개 비공선 원자가 필요합니다. 화면에서도 anchor를 수정할 수 있습니다. 입력/규칙 해시가 바뀌면 이전 그룹 뷰를 자동 재사용하지 않습니다.
- `--force-refresh`는 출력 재다운로드와 재파싱을 수행합니다. 기존 `-Offline`은 여전히 시트 재다운로드만 생략합니다.
- CLI는 기존 DOCX 경로를 덮어쓰지 않습니다. 의도적으로 덮어쓸 때만 `--force-output`을 사용합니다. GUI는 실행마다 별도 폴더를 만듭니다. 작업 중 `작업 취소`가 가능합니다.
- `report_layout`에서 표 간격과 셀 여백을 조절합니다. 기존 설정에 새 키가 없어도 기본값으로 동작하고 기존 설정/문장/계획 파일을 초기화하지 않습니다. 예전 문장 파일에 새 템플릿 키가 없으면 C++ 기본 문장을 사용합니다.
- 스핀 `Rest`/`Sum`은 해당 구조의 전체 원자 spin 데이터가 있어야 계산합니다. 불완전한 경우 `N/A`이며 0으로 채우지 않습니다.
- 단계별 시간·다운로드 수·바이트·캐시 적중·SSH 실행 수를 stdout과 모델 `metrics`에 기록합니다. 현재 타이머는 밀리초 단위이므로 아주 짧은 로컬 단계는 0 ms로 나올 수 있습니다.

별도 빌드와 패키지:

```powershell
.\tools\build.ps1 -BuildDirectory build-upgrade
.\tools\package.ps1 -BuildDirectory build-upgrade -OutputPath outputs/upgrade-validation/Youngkly.exe
```

패키징 기본 출력은 `outputs/package/Youngkly.exe`입니다. 기존 실행 파일을 교체하지 않으며 지정 출력이 이미 있으면 중단합니다. 테스트는 core, upgrade, render_controls, future_editor의 네 묶음입니다. 검증 내역과 제한은 `docs/upgrade-validation.md`를 참고하세요.

## SI Generator 형식 통합 및 Windows 업데이트

Youngkly의 Word 출력은 SI Generator 방식의 구조별 행(Spin Density / Selected Geometry), 3선 테이블(0.75 pt), 구조명 위첨자·아래첨자, Figure/Table 자동 SEQ 번호를 사용합니다. Figure에는 고해상도 정적 PNG를 삽입하고 TS GIF는 별도 파일로 보존합니다. 보고서 실행 과정에서 IRC 방향을 확인하면 반응물·생성물 기준 에너지 문장이 갱신됩니다. 방향을 확정하지 않은 경로는 활성화 장벽의 정방향 해석을 보류합니다.

CI는 `.github/workflows/windows-build.yml`에 있으며 Windows에서 전체 C++ 빌드, 기본 회귀 시험, SI 형식 Word DOCX 스모크 검사, 단일 실행 파일 패키징을 수행합니다. SI Generator와 Youngcraft의 정확한 의존 커밋은 `config/dependencies.lock.json`에 고정되어 있습니다. OpenGL의 실제 그래픽 품질과 원격 서버 데이터는 사용자 Windows 환경에서 별도로 검증해야 합니다.

최신 `main`의 빌드 파일을 기존 프로그램이 있는 저장소에 설치하려면 PowerShell에서 실행합니다.

```powershell
gh auth login
.\tools\update-local.ps1
```

Git과 GitHub CLI(`gh`)가 필요합니다. 스크립트는 `main`을 fast-forward로 갱신한 다음 **현재 Git 커밋에 대응하는 성공한 Actions 실행**의 `Youngkly-Windows-portable` 아티팩트를 받아 기존 `Youngkly.exe`를 `backups/`에 백업하고 새 파일로 교체합니다. 사용 중인 Youngkly를 종료한 뒤 실행하세요. 첫 실행 시 내부 런타임은 별도 버전 폴더에 자동 생성됩니다.

GitHub 웹에서 `Actions → Youngkly Windows build → 성공한 main 실행 → Artifacts → Youngkly-Windows-portable`로 직접 다운로드하여 압축을 풀고 `Youngkly.exe`를 교체해도 됩니다. 사용자가 수정한 설정, Future work 및 보고서 폴더는 유지하세요.


## 날짜별 프로젝트 저장과 위클리 표기

`프리뷰 생성·저장`은 선택한 날짜의 모든 연구·탭을 **보고서 하나의 프로젝트**로 저장합니다. 예: `프로젝트 모음/2026년 11월 2주차 주간 보고서 (11-05)/` 안에 같은 이름의 `.youngkly_project.json`과 Word 보고서를 둡니다. `자료/저장 001` 등에는 이전 저장 상태와 그림·설정·Future work를 보존합니다. SI Generator처럼 프로젝트 파일을 다시 열어 프리뷰를 확인하고 갱신하거나 Word를 작성합니다. `생성된 Word 열기`와 `결과 폴더 열기`는 현재 보고서에 연결됩니다. Google Sheets → 서버 탐색 → SI 분석 → 그림 → Word 흐름을 유지하며, 시트 행에 lion/dumbo/tachyon 또는 한글 서버명이 있으면 그 서버에서 찾습니다. 표기가 없으면 기존 서버 우선순위를 따릅니다.

데이터와 영문은 Times New Roman, 한글은 맑은 고딕을 사용합니다. 스핀 상태는 *S = 0*, *S = 1*, *S = 2*로 표시하며 반정수도 지원합니다. Figure/Table의 SEQ 번호는 Word 갱신 후에도 굵게 유지하도록 필드 전체를 서식 지정하고, 마침표 뒤 공백 한 칸을 보존합니다. 위클리 상대 에너지 표에는 a/b/c 첨자와 각주 설명을 넣지 않습니다. Dispersion·Complexation은 기본 제외이며 필요할 때 체크하여 포함합니다.

Spin density는 SI Generator의 단계별 기준과 동일하게 모든 비교 구조에 공통인 원자를 추려 최대 절댓값 순서로 6개를 선택합니다. Rest/Sum은 전체 원자 데이터가 완전할 때 계산합니다. Selected geometry 자동 선택은 SI의 `suggestGeometry(records, 6)`를 직접 사용하여 결합 변화와 정렬 후 원자 이동을 반영합니다. 자동 선택은 최대 6개이며, 명시적으로 지정한 geometry는 사용자 선택을 유지합니다. 표에는 원소기호만 표시하고 원자 ID는 모델에 별도 보존합니다. 표의 위아래 셀 여백 기본값은 0 pt이고 고정 열 너비와 동일한 줄 간격을 사용합니다.


처음 실행하면 달력에서 보고 날짜를 선택하고 **새 보고서 프로젝트 만들기**를 누릅니다. 시트나 서버 연결 전에 프로젝트 파일이 만들어져 달력 아래 목록에 즉시 표시됩니다. 같은 날짜의 프로젝트가 있으면 기존 보고서를 엽니다. 연구를 선택하고 **시트·서버에서 자료 가져오기**를 누르면 Google Sheets 조회와 서버 수집·분석·그림 생성을 진행합니다. 자료가 준비되면 프리뷰를 확인하고 **현재 프리뷰로 Word 작성**을 누릅니다.
