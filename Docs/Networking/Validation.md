# 검증 상태

- Editor C++ 빌드: 기존 구현 통과 (`Saved/Logs/DormancyFinalBuild.txt`), 코드 구조 분리 후 최종 빌드도 통과 (`Saved/Logs/DormancyRefactorBuild.txt`).
- 에디터 AssetTools 허브 이동: 완료. ResavePackages fixupredirects로 이전 리다이렉터 삭제 로그 확보.
- Basic 템플릿 데모맵: 생성·저장 및 Blueprint 컴파일 통과. 두 맵 로드, World Partition 없음, use_external_actors=false, 외부 패키지 없음, 이전 허브 Asset Registry 참조 없음 확인 (`Saved/DormancyAssetValidation.txt`).
- 별도 서버 + 두 클라이언트: 실제 접속 및 Initial/Converge 상태 일치 확인.
- 관찰 클라이언트 실행 요청: 실제 서버 거부 로그 확인.
- 4,096개 액터 Baseline/Dormancy/Frequent Changes: 6조건 × 3회 = 18회 완료. 9개 A/B 쌍의 설정/seed/step 수/변경 건수/최종 상태 digest 동일. 전후 trace와 Insights CSV 확보.
- 집계 함수: idle 구간 합집합 및 nearest-rank p95 경계 테스트 통과. 테스트용 표본은 실험 결과에 포함하지 않음.
- 지속 Awake: 통과. 1,024개 중 102개 cohort, 60 step/6,120 changes, explicit wake 102회/explicit flush 0회. 종료 후 두 클라이언트 최종 일치 (`validation-continuous`).
- Late join: 통과. 첫 클라이언트로 변경 완료 후 두 번째 접속, `LateJoinMatch:257` 확인 (`validation-latejoin`).
- 지연·손실: 통과. 서버 로그에서 PktLag=100ms/PktLoss=5% 적용 확인, 30 step/3,060 changes/flush 3,060회 후 2/2 최종 일치 (`validation-loss`). 이는 해당 에뮬레이션 조건의 정확성 검증이며 원격 네트워크 성능 측정이 아니다.
- 잘못된 액터 수 거부, 관찰자 시작 거부, 실행 중 설정 변경 거부, 정지→수렴→초기화→재실행: 통과 (`validation-controls`).
- 서버 RPC 전원 맵 왕복: 통과 (`validation-travel-v3`). 메인은 `LAB_TRAVEL_COMPLETE Main=1`, 관찰자는 `Main=0`. 첫 검사에서 기존 포털의 dedicated-server-stripped TextRender null 접근을 발견해 수정했다. 컨트롤러 교체 시 메인 역할을 SeamlessTravelFrom으로 복사하고 새 월드의 HUD 타이머를 복구했다. 실패 실행 로그도 보존한다.
- 실제 Insights 캡처와 Networking Insights 연결/액터별 packet-content 드릴다운: 미완료. Computer Use가 물리 Esc 입력으로 중단되어 추가 화면 조작을 수행하지 않았다. net 채널 원본 trace는 확보했으나 UI 분석/이미지로 대체 주장하지 않는다.
- 이동·점프·카메라, HUD 조작 및 닫은 뒤 입력 복귀, 이미지 확대·스크롤, 실제 사용자 조작에 의한 전원 맵 왕복: 사용자 수동 확인 전 미검증.
- 패키징/cook 및 패키징 Dedicated Server 실행: 미검증. 현재 검증은 installed Editor 기반 별도 server/client 프로세스다.

자동 드라이버는 HUD와 같은 ServerLabAction RPC/서버 Request 경로를 사용한다. 자동 결과를 실제 UI 조작 검증으로 표현하지 않는다.

## 코드 구조 분리 후 회귀 검증 (2026-09-27)

아래 회귀 검증에 사용한 모듈 SHA256: `B03026CB1991D4CACBB88B88B8CD7E8041FCFBBB4FBC382161A042732A5F9A82`.
아래 실행은 installed Editor 기반 별도 서버와 headless 클라이언트로 수행한다. 액터 수 256, warm/stable/change 각 2초이며 성능 비교 통계에 포함하지 않는다. 원본 로그/trace는 `Saved/DormancyRuns/`의 각 실행 폴더에 남긴다.

| 실행 | 확인 항목 | 상태 |
|---|---|---|
| `refactor-final-controls` | 컴포넌트 RPC, 잘못된 설정/관찰자 요청 거부, 실행 중 설정 잠금, 정지·초기화·재실행, 2/2 상태 일치 | 통과 |
| `refactor-final-travel` | Hub → Dormancy → Hub 전원 이동, 메인/관찰자 역할 유지, 기존 맵의 native 클래스 로드 | 통과 |
| `refactor-final-continuous` | 26개 cohort, 40 step/1,040 changes, wake 26회/flush 0회, 종료 후 2/2 수렴 | 통과 |
| `refactor-final-loss` | PktLag 100ms / PktLoss 5%에서 단발 flush 후 2/2 수렴 | 통과 |
| `refactor-final-latejoin` | Dormant 상태에서 실행 완료 후 늦은 접속자의 현재 상태 수신, LateJoinMatch 확인 | 통과 |

실제 키·마우스 입력과 화면 캡처는 수행하지 않았다. 구조 분리 후에도 패널 조작·입력 복귀·이미지 확대 및 이동·카메라 조작은 사용자 확인 전 미검증이다.

이후 추가된 선언 배치 규칙을 공용·Dormancy 헤더에 적용했다. 함수/변수 순서, 역할별 묶음, RPC 묶음, 매크로 선언 간격을 정리했으며 UHT 및 Editor 빌드가 통과했다 (`Saved/Logs/DeclarationLayoutBuild.txt`). 이번 선언 정리에서는 네트워크 회귀 실행과 실제 조작 검증을 다시 수행하지 않았다.

오버라이드 접근 수준도 UE 5.8.3의 부모 헤더와 대조했다. DormancyLab의 BeginPlay/EndPlay는 protected, DormancyClientComponent의 BeginPlay/EndPlay와 TechLabCharacter의 SetupPlayerInputComponent는 public으로 맞췄다. 나머지 오버라이드는 이미 일치했다. UHT 및 Editor 빌드 통과 (`Saved/Logs/OverrideAccessBuild.txt`); 접근 지정만 변경하여 실행·조작 검증은 반복하지 않았다.

부모 동명 함수와 구현을 추가 점검하여 DormancyLab의 암묵적 `AActor::Reset` 재정의를 제거했다. 실험 초기화는 `ResetExperiment`로 분리하고 Actor의 `Reset`/`K2_OnReset`은 상속한 동작을 유지한다. Character의 입력 설정에는 `Super::SetupPlayerInputComponent`를 추가해 부모의 입력 컴포넌트 검사를 보존했다. UHT/Editor 빌드 통과 (`Saved/Logs/ParentContractBuild.txt`). 별도 서버+두 headless 클라이언트의 잘못된 설정/관찰자 요청 거부, 실행 중 설정 잠금, 정지→초기화→재실행과 2/2 최종 상태 일치도 통과했다 (`parent-contract-controls`, 256개, warm/stable/change 각 2초). 실제 이동·카메라·UI 조작을 검증한 결과는 아니다.

수동 확인은 `Scripts/StartDormancyInteractive.ps1`로 시작한다. 허브/별도 서버+두 클라이언트에서 이동·점프·카메라, `/` 패널 펼치기/접기 후 입력 복귀, MAIN의 전원 이동과 실험 조작, OBSERVER의 조작 제한/결과 열람을 확인한다. 기대 결과 상세는 DormancyDemo.md의 표 참조. 현재 실제 PNG 캡처가 없으므로 이미지 표시/확대 동작은 캡처 추가 후 검증한다.
