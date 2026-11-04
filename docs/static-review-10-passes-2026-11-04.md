# Youngkly 정적 코드 검토 — 10개 독립 회독 관점

- **작성일:** 2026-11-04 (Asia/Seoul)
- **대상:** `Skylark51/Youngkly`, `main`의 초기 커밋 `b273260600bb069731359c6fcfd0eecf6d97032a`
- **방식:** GitHub 소스의 정적 검토. 회독별로 **새로운 문제**를 배정했으며 다음 회독에서는 같은 지적을 재사용하지 않았다.
- **범위:** 데이터 수집 → 반응 경로/에너지/스핀 → 정렬/렌더링 → DOCX → 배포 및 테스트를 잇는 핵심 C++·설정·스크립트·회귀 테스트 확인.
- **검증 한계:** 이 검토에서는 Windows 앱, SSH, Gaussian, Word를 새로 실행하지 않았다. 이전 로컬 작업 기록의 CTest 4/4 통과는 *과거 실행 결과*이며 본 문서의 결함이 재현되거나 수정되었다는 뜻이 아니다. 외부 SI Generator와 Youngcraft 구현까지 전수 검증한 것은 아니다.

## 회독 1 — XLSX 수식/셀 해석 (중간)

**관찰:** 공유 수식 번역은 정규식 기반으로 `A1` 참조를 치환한다. `translateFormula`는 큰따옴표 내부를 걸러내지만 표 이름·외부 통합문서 참조·구조화된 참조·시트명 등의 전체 Excel 문법을 파싱하지 않는다. 미지원 수식의 캐시 값이 나중에 보고서에 사용될 때 수식의 의미를 독립적으로 확인하기 어렵다.

**근거:** [`src/Sheet.cpp:49–76`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Sheet.cpp#L49-L76), [`src/Sheet.cpp:120–165`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Sheet.cpp#L120-L165)

**개선:** 공유 수식에 대해 지원 구문을 문서화하고, 범위를 벗어난 패턴은 `formula_unverified`로 표시한다. 시트명·절대 참조·문자열·구조화된 참조를 포함한 XLSX fixture를 추가한다. **확정 버그라기보다는 파서 지원 범위의 제약**이다.

## 회독 2 — 반응 경로 자동 그룹화 (높음)

**관찰:** `makePlan`에서 출력 파일 확장자는 `.out`과 `.log`를 모두 인식하지만, IRC triplet 결합 조건의 suffix는 `.out`만 하드코딩되어 있다. 따라서 정상적인 `.log` IRC endpoint 2개와 TS의 triplet은 매칭에서 누락된다. 연속 세 행의 **section 동일 여부도 검사하지 않으므로**, 동일 stem이 서로 다른 연구 소절을 가로지르는 경우 잘못 묶일 가능성이 있다.

**근거:** [`src/Sheet.cpp:191–195`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Sheet.cpp#L191-L195), [`src/Sheet.cpp:241–258`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Sheet.cpp#L241-L258)

**개선:** `.out/.log`는 TS의 실제 확장자를 이용하여 endpoint suffix를 생성한다. `a.section == b.section == c.section`를 필수 조건으로 추가한다. 두 문제 각각에 대해 정상/오류 fixture를 작성한다.

## 회독 3 — 화학적 장벽의 기준 구조 (매우 높음)

**관찰:** `analyze`는 triplet의 시트 첫 번째 구조를 기준으로 `TS – 첫 구조`를 activation barrier라 표현한다. 하지만 시트가 IRC forward→TS→IRC reverse 또는 역순 모두 허용되고, 첫 endpoint가 반응물인지 생성물인지 확인하는 필드는 없다. 그래서 **역방향 장벽을 정방향 장벽처럼 문장화할 가능성**이 있다.

**근거:** [`src/Sheet.cpp:241–259`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Sheet.cpp#L241-L259), [`src/Pipeline.cpp:288–303`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Pipeline.cpp#L288-L303)

**개선:** 그룹에 `reactant_row_id`, `product_row_id`, `reaction_direction_confirmed`를 도입한다. 미확정이면 양방향 `ΔE‡`만 수치로 제시하고 '정방향 활성화 장벽' 문장을 보류한다. TS와 두 endpoint의 에너지를 입력한 방향 반전 회귀 테스트를 추가한다.

## 회독 4 — 에너지/보정항 정합성 (매우 높음)

**관찰:** `energyMatch`가 검사하는 SI 상대 차이는 `c < 6`인 L–Q 열뿐이다. R(분산) 및 S(최종 ΔG)는 독립적인 SI 대조 대상이 아니다. 또한 `complete`가 구조별로 초기화되지 않아 앞 구조에 빈 셀이 있으면 뒤 구조의 완전한 행에서도 산술 검사를 생략한다. 계산된 상대에너지, 시트의 캐시 값 및 표현식 간 검증 범위가 달라질 수 있다.

**근거:** [`src/Pipeline.cpp:209–274`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Pipeline.cpp#L209-L274), [`src/RelativeTable.cpp:18–47`](https://github.com/Skylark51/Youngkly/blob/b273260/src/RelativeTable.cpp#L18-L47)

**개선:** `rowComplete`와 `groupComplete`를 분리하고, `E Total`, ZPE, thermal, `−TΔS`, 분산, complexation 및 `ΔG`에 각각 `missing/matched/mismatch/not_applicable` 상태를 부여한다. 실제 단일점·열보정 출력과 XLSX 수식 캐시값을 조합한 오염 fixture로 검증한다.

## 회독 5 — BB 단일점의 화학적 상태 동일성 (매우 높음)

**관찰:** BB 채택 조건은 주로 정상 종료, `TZVPP` basis, 원소/원자 순서와 정렬 후 구조 일치다. **전하, 다중도, SCF 방법/용매 모델**의 동일성은 이 함수의 명시적 승인 조건에 없다. 같은 좌표와 basis의 다른 전자 상태 BB 로그가 입력되면, 단일점 에너지와 스핀을 잘못 조합할 위험이 있다.

**근거:** [`src/Pipeline.cpp:81–119`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Pipeline.cpp#L81-L119), [`src/GaussianStore.cpp:17–40`](https://github.com/Skylark51/Youngkly/blob/b273260/src/GaussianStore.cpp#L17-L40)

**개선:** Gaussian 파서 결과에 `charge`, `multiplicity`, `method`, `solvation`, `dispersion` 메타데이터를 포함하고 pairing 시 기대값을 강제한다. 하나라도 불일치하면 BB를 제외하고 mismatch 사유를 보고한다. 외부 SI 파서에도 해당 정보가 있는지는 별도로 확인한다.

## 회독 6 — 원자 매핑의 검증 경계 (높음)

**관찰:** 매핑이 없으면 같은 인덱스의 원소가 같다는 이유로 항등 대응을 후보로 생성한다. 구조적으로 동일한 원소가 여러 개인 경우 원자 정체성을 보장하지 못한다. 사용자 확인 절차는 있지만 `atom_mapping_confirmed`는 체크 여부만 기록하므로, 대응 검증의 판단 근거(지정 bond 변화·정렬 RMSD·미확정 대칭 원자)가 남지 않는다.

**근거:** [`src/AtomMapping.cpp:5–28`](https://github.com/Skylark51/Youngkly/blob/b273260/src/AtomMapping.cpp#L5-L28), [`src/Pipeline.cpp:157–185`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Pipeline.cpp#L157-L185), [`src/render_main.cpp:90–102`](https://github.com/Skylark51/Youngkly/blob/b273260/src/render_main.cpp#L90-L102)

**개선:** 매핑에 atom index, element, 결합 이웃, anchor RMSD를 함께 저장한다. 동등 원소 치환 후보가 있으면 원자 식별을 강조한 UI 확인 목록을 제공한다. 이 항목은 **의도적으로 수동 확인하도록 설계된 영역의 검증 강화 제안**이다.

## 회독 7 — SSH 전송 및 캐시 일관성 (중간)

**관찰:** 로컬 캐시 적중은 원격 파일의 크기·수정 시간과 로컬 SHA-256으로 판단한다. 원격 파일이 **동일한 크기와 mtime으로 교체**되면 기존 캐시가 사용될 수 있다. 기존 검증 문서에서도 같은 제약을 명시한다. 전송 전후 metadata 비교로 일반적인 갱신 도중 경쟁 조건을 완화하지만 원격 내용 동일성을 증명하지는 못한다.

**근거:** [`src/FileTransfer.cpp:130–155`](https://github.com/Skylark51/Youngkly/blob/b273260/src/FileTransfer.cpp#L130-L155), [`docs/upgrade-validation.md`](https://github.com/Skylark51/Youngkly/blob/b273260/docs/upgrade-validation.md)

**개선:** 원격 SHA-256을 선택적으로 계산해 캐시를 재검증하는 `--verify-remote-hash` 모드를 추가한다. 재현성 중심의 최종 보고서에서는 사용한 원본과 BB의 원격 digest를 manifest에 남긴다. 대량 파일에는 비용이 있으므로 기본 모드와 별도로 제공한다.

## 회독 8 — 렌더링 메모리와 실패 복원력 (중간)

**관찰:** 렌더러는 구조별 고해상도 PNG와 GIF 32프레임을 모두 메모리에 보관한 뒤, 공통 crop을 계산하고 저장한다. 분자 규모·해상도·그룹 수가 커지면 프레임 저장 공간과 crop용 복제본이 커진다. 예외가 나면 현재 그룹 처리와 전체 렌더 흐름이 종료되므로 이전 그룹의 완료 결과와 실패 그룹을 독립 재시도하기 어렵다.

**근거:** [`src/render_main.cpp:173–211`](https://github.com/Skylark51/Youngkly/blob/b273260/src/render_main.cpp#L173-L211), [`src/render_main.cpp:224–236`](https://github.com/Skylark51/Youngkly/blob/b273260/src/render_main.cpp#L224-L236)

**개선:** 첫 패스는 bounding box/crop만 계산하고, 두 번째 패스에서 프레임을 생성·저장한다. 그룹별 `render_error`와 재시도 가능 상태를 보존하며 완성된 asset의 fingerprint가 맞으면 재활용한다. 4개 스핀 상태와 32프레임에서 최대 RSS 및 시간 측정값을 회귀 지표로 저장한다.

## 회독 9 — Word 템플릿의 잔존 데이터 (높음)

**관찰:** `writeReport`는 참조 DOCX의 ZIP 전체를 복사한 뒤 주로 `word/document.xml`, 관계, 콘텐츠 타입, settings를 갱신한다. 원본의 사용하지 않는 media, comments, custom XML, 첨부/관계, 작성자 메타데이터 등이 결과 DOCX에 남을 수 있다. 공개 저장소에는 원본 보고서 DOCX 자체도 포함되어 있어 배포 전 정보 검토가 필요하다.

**근거:** [`src/Docx.cpp:233–243`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Docx.cpp#L233-L243), [`src/Docx.cpp:488–512`](https://github.com/Skylark51/Youngkly/blob/b273260/src/Docx.cpp#L488-L512)

**개선:** 결과 DOCX에는 실제 참조되는 OOXML parts만 포함하도록 relationship graph를 따라 새 패키지를 구성한다. author/comments/tracked changes 및 미사용 이미지의 제거 기준을 명시한다. 출력 DOCX에 이전 보고서의 텍스트·이미지/주석·개인 메타데이터가 없는지 ZIP 검사 테스트를 작성한다.

## 회독 10 — 재현 가능한 빌드·운영 및 테스트 자동화 (높음)

**관찰:** CMake는 저장소 외부의 로컬 `SI Generator`·`Youngcraft` 소스를 절대/상대 경로로 직접 포함하고 Qt/MinGW를 로컬 설치에 의존한다. 소스/버전이 저장소와 함께 고정되어 있지 않아 GitHub만으로 동일한 바이너리를 재현하기 어렵다. 현재 CTest에는 4개 테스트 타깃이 있지만 GitHub Actions 워크플로가 저장소 트리에 없고, 외부 계산 출력 기반 통합 테스트는 동봉되지 않았다. GUI의 보고서 파일명도 작성자 이름을 하드코딩한다.

**근거:** [`CMakeLists.txt:1–7`](https://github.com/Skylark51/Youngkly/blob/b273260/CMakeLists.txt#L1-L7), [`CMakeLists.txt:27–47`](https://github.com/Skylark51/Youngkly/blob/b273260/CMakeLists.txt#L27-L47), [`CMakeLists.txt:104–121`](https://github.com/Skylark51/Youngkly/blob/b273260/CMakeLists.txt#L104-L121), [`src/app_main.cpp:211–216`](https://github.com/Skylark51/Youngkly/blob/b273260/src/app_main.cpp#L211-L216)

**개선:** 외부 의존성의 고정 commit/라이선스/빌드 절차를 manifest에 저장하고 CMake preset 또는 FetchContent/submodule(사용 권한 확인 후)을 도입한다. Windows CI에서 CTest, fixture 분석, DOCX 패키지 검사, OpenGL 가능한 환경의 스모크 테스트를 순차 실행한다. 보고서 사용자명/저장 경로를 config 항목으로 이동한다.

## 우선순위 제안

| 순서 | 작업 | 완료 판정 |
|---|---|---|
| **P0-1** | 반응 방향·반응물/생성물 확인 (회독 3) | 미확정 방향에서는 장벽 문장 생성 불가, 방향 반전 fixture 통과 |
| **P0-2** | BB 전자 상태 메타데이터 검증 (회독 5) | charge/multiplicity/method/solvent 불일치가 자동 거부 |
| **P0-3** | 에너지 항목별 검증과 행 독립 completeness (회독 4) | 누락값/분산/Gibbs 의도적 오염 fixture 탐지 |
| **P1-1** | triplet 확장자·section 수정 (회독 2) | `.log` 세 구조가 묶이고 교차 section은 거부 |
| **P1-2** | DOCX 패키지 정리 (회독 9) | 예전 메타데이터·주석·미사용 media 부재 |
| **P1-3** | 원자 매핑 검증 증거 (회독 6) | 대칭/동일 원소 원자 교환 후보 경고 및 선택 기록 |
| **P2** | 수식 지원 범위, 캐시 검증, 렌더 재시도, CI/빌드 재현성 (회독 1·7·8·10) | 각각 별도 fixture/벤치마크 및 CI에서 확인 |

## 연구 정확도 중심 구현 순서

1. **과학적 데이터 계약 먼저 확장:** 각 파일에서 electronic-state metadata를 추출하고, 경로별 reactant/product/TS와 기준 에너지의 의미를 명시한다.
2. **검증 결과를 모델의 구조화된 필드로 저장:** `validated`, `rejected`, `requires_review` 및 이유·파일 SHA-256·검증 규칙 버전을 남긴다.
3. **Word 문장은 검증 상태를 소비하도록 변경:** 검증 실패·불완전 데이터는 표의 주석과 검토 표시만 남기고 확정적인 해석 문장을 막는다.
4. **회귀 fixture 확충 후 배포:** 정확도 수정과 배포 시스템 변경을 분리해 검증한다.

**상태:** 수정 제안서만 작성했다. 애플리케이션 소스 변경이나 새 Windows 빌드/런타임 테스트는 이 문서 작성 과정에서 수행하지 않았다. 각 지적의 '가능성'은 실패 재현 전에는 확정 결함으로 취급하지 않는다.
